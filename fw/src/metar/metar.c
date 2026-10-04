/*
 * METAR fallback (spec section 14): the station fetches the report of the
 * nearest airport itself, so the sign shows the street when the server
 * is silent. Polls always (every metar.period minutes, 30 by default) so
 * that obs.* are ready at the moment of a failure and can be put on screens.
 *
 * HTTPS with server verification: built-in roots (ca_bundle.pem) or the
 * metar.ca setting. The first URL is tried first, the second on any error.
 * Requests start only after the clock is synced (certificate checks and the
 * observation age need the date).
 *
 * The mapping to variables is fw/lib/metar_map (ztest); this file is glue.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/shell/shell.h>
#include <zephyr/zbus/zbus.h>

#include <ws/metar_map.h>
#include <ws/settings.h>

#include "../app.h"
#include "../net/http_get.h"
#include "../net/net_mgr.h"
#include "metar.h"

LOG_MODULE_REGISTER(ws_metar, LOG_LEVEL_INF);

#define TAG_BUILTIN 0x57530001
#define TAG_USER    0x57530002
#define BODY_MAX    1024

extern const unsigned char ws_metar_ca_bundle[];
extern const unsigned int ws_metar_ca_bundle_len;

static struct {
	struct k_mutex lock;  /* status */
	struct k_mutex fetch; /* one fetch at a time (work queue or shell) */
	char body[BODY_MAX + 1];
	size_t body_len;
	char raw[WS_METAR_MAX_LEN];
	char user_ca[sizeof(((struct ws_settings *)0)->ca_pem)];
	bool user_ca_set;
	struct ws_metar_status st;
} m;

static void fetch_work_fn(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(fetch_work, fetch_work_fn);
static K_THREAD_STACK_DEFINE(metar_stack, CONFIG_WS_METAR_STACK_SIZE);
static struct k_work_q metar_q;

static int body_cb(const uint8_t *data, size_t len, size_t total, void *user)
{
	size_t n = MIN(len, BODY_MAX - m.body_len);

	memcpy(m.body + m.body_len, data, n);
	m.body_len += n;
	return 0; /* a longer body is cut: the report is near the top */
}

/* Installs the user CA from the settings (or removes it). */
static void update_user_ca(const struct ws_settings *s)
{
	if (m.user_ca_set) {
		tls_credential_delete(TAG_USER, TLS_CREDENTIAL_CA_CERTIFICATE);
		m.user_ca_set = false;
	}
	if (!s->ca_pem[0]) {
		return;
	}
	strncpy(m.user_ca, s->ca_pem, sizeof(m.user_ca) - 1);
	int ret = tls_credential_add(TAG_USER, TLS_CREDENTIAL_CA_CERTIFICATE, m.user_ca,
				     strlen(m.user_ca) + 1);

	if (ret) {
		LOG_ERR("metar.ca: %d", ret);
		return;
	}
	m.user_ca_set = true;
}

/* Decodes and stores a report. Also used by the shell for bench tests. */
int ws_metar_ingest(const char *raw)
{
	struct ws_metar_obs obs;
	struct ws_metar_out out;
	struct ws_value sun;
	struct ws_now now = ws_app_now();

	if (ws_metar_decode(raw, &obs)) {
		return -EINVAL;
	}
	ws_app_lock();
	struct ws_vars *v = ws_app_vars();
	int sun_up = ws_vars_get(v, WS_V_SUN_UP, &now, &sun) ? (int)sun.num : -1;

	ws_metar_map(&obs, sun_up, now.unix_s, &out);
	ws_metar_apply(v, &out, now.mono);
	ws_app_unlock();
	ws_app_vars_touched();

	k_mutex_lock(&m.lock, K_FOREVER);
	strncpy(m.st.report, raw, sizeof(m.st.report) - 1);
	m.st.report[sizeof(m.st.report) - 1] = '\0';
	m.st.last_ok = now.unix_s;
	m.st.obs_unix = out.obs_unix;
	k_mutex_unlock(&m.lock);
	LOG_INF("METAR %s", raw);
	return 0;
}

static int fetch_one(const struct ws_settings *s, int which)
{
	char url[sizeof(s->metar_url1) + 8];
	sec_tag_t tags[1] = {s->ca_pem[0] ? TAG_USER : TAG_BUILTIN};

	if (ws_settings_metar_url(s, which, url, sizeof(url)) < 0) {
		return -ENOENT;
	}
	struct ws_http_get req = {
		.url = url,
		.tags = tags,
		.n_tags = 1,
		.timeout_ms = 15000,
		.body = body_cb,
	};

	m.body_len = 0;
	int ret = ws_http_get(&req);

	if (ret) {
		return ret;
	}
	m.body[m.body_len] = '\0';
	if (ws_metar_extract(m.body, m.body_len, s->metar_icao, m.raw, sizeof(m.raw)) < 0) {
		LOG_WRN("no %s report in the response", s->metar_icao);
		return -ENODATA;
	}
	return ws_metar_ingest(m.raw);
}

int ws_metar_fetch_now(void)
{
	struct ws_settings s;
	int ret = -ENOENT;

	ws_app_settings_get(&s);
	if (!s.metar_icao[0]) {
		return -ENOENT;
	}
	if (!ws_app_time_synced()) {
		return -EAGAIN;
	}
	k_mutex_lock(&m.fetch, K_FOREVER);
	for (int which = 0; which < 2; which++) {
		ret = fetch_one(&s, which);
		k_mutex_lock(&m.lock, K_FOREVER);
		m.st.attempts++;
		m.st.last_err = ret;
		m.st.source = ret ? m.st.source : which;
		k_mutex_unlock(&m.lock);
		if (!ret) {
			break;
		}
		LOG_WRN("METAR URL %d failed: %d", which + 1, ret);
	}
	k_mutex_unlock(&m.fetch);
	return ret;
}

static void fetch_work_fn(struct k_work *w)
{
	struct ws_settings s;

	ws_app_settings_get(&s);
	if (!ws_net_online() || !ws_app_time_synced()) {
		/* not yet: look again in a minute */
		k_work_reschedule_for_queue(&metar_q, &fetch_work, K_SECONDS(60));
		return;
	}
	int ret = ws_metar_fetch_now();
	/* on failure retry sooner, but not more often than every 5 minutes */
	int period = MAX(s.metar_period, 10) * 60;

	k_work_reschedule_for_queue(&metar_q, &fetch_work,
				    K_SECONDS(ret && ret != -ENOENT ? MIN(period, 300) : period));
}

void ws_metar_status_get(struct ws_metar_status *out)
{
	k_mutex_lock(&m.lock, K_FOREVER);
	*out = m.st;
	k_mutex_unlock(&m.lock);
}

static void settings_listener_cb(const struct zbus_channel *chan)
{
	struct ws_settings s;

	ws_app_settings_get(&s);
	update_user_ca(&s);
	/* new station or URLs: fetch soon */
	k_work_reschedule_for_queue(&metar_q, &fetch_work, K_SECONDS(2));
}
ZBUS_LISTENER_DEFINE(ws_metar_settings_lis, settings_listener_cb);
ZBUS_CHAN_ADD_OBS(ws_chan_settings, ws_metar_settings_lis, 3);

int ws_metar_start(void)
{
	struct ws_settings s;
	int ret;

	k_mutex_init(&m.lock);
	k_mutex_init(&m.fetch);
	m.st.last_err = -EAGAIN;
	ret = tls_credential_add(TAG_BUILTIN, TLS_CREDENTIAL_CA_CERTIFICATE, ws_metar_ca_bundle,
				 ws_metar_ca_bundle_len + 1);
	if (ret) {
		LOG_ERR("CA bundle: %d", ret);
	}
	ws_app_settings_get(&s);
	update_user_ca(&s);
	k_work_queue_start(&metar_q, metar_stack, K_THREAD_STACK_SIZEOF(metar_stack),
			   K_LOWEST_APPLICATION_THREAD_PRIO,
			   &(struct k_work_queue_config){.name = "metar"});
	k_work_reschedule_for_queue(&metar_q, &fetch_work, K_SECONDS(10));
	return 0;
}

/* ---- shell ---- */

static int cmd_fetch(const struct shell *sh, size_t argc, char **argv)
{
	int ret = ws_metar_fetch_now();

	shell_print(sh, "fetch: %d", ret);
	return ret ? -ENOEXEC : 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct ws_metar_status st;

	ws_metar_status_get(&st);
	shell_print(sh, "report: %s", st.report[0] ? st.report : "-");
	shell_print(sh, "last_ok: %lld", (long long)st.last_ok);
	shell_print(sh, "obs_time: %lld", (long long)st.obs_unix);
	shell_print(sh, "source: %d", st.source + 1);
	shell_print(sh, "last_err: %d", st.last_err);
	shell_print(sh, "attempts: %u", st.attempts);
	return 0;
}

/* ws metar parse UNNT 041130Z ... : feed a report without the network */
static int cmd_parse(const struct shell *sh, size_t argc, char **argv)
{
	static char raw[WS_METAR_MAX_LEN];
	size_t n = 0;

	raw[0] = '\0';
	for (size_t i = 1; i < argc; i++) {
		int k = snprintk(raw + n, sizeof(raw) - n, "%s%s", i > 1 ? " " : "", argv[i]);

		if (k < 0 || (size_t)k >= sizeof(raw) - n) {
			shell_error(sh, "too long");
			return -ENOEXEC;
		}
		n += k;
	}
	if (ws_metar_ingest(raw)) {
		shell_error(sh, "not a METAR report");
		return -ENOEXEC;
	}
	shell_print(sh, "ok");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	metar_cmds, SHELL_CMD_ARG(fetch, NULL, "Fetch the report now", cmd_fetch, 1, 0),
	SHELL_CMD_ARG(status, NULL, "Last report and errors", cmd_status, 1, 0),
	SHELL_CMD_ARG(parse, NULL, "Apply a raw report: parse <METAR...>", cmd_parse, 2, 30),
	SHELL_SUBCMD_SET_END);
SHELL_SUBCMD_ADD((ws), metar, &metar_cmds, "METAR fallback", NULL, 0, 0);
