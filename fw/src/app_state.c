/* Shared state, configuration swap, clock and zbus channels (see app.h). */
#include <stdio.h>
#include <string.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include <ws/util.h>

#include "app.h"
#include "storage/storage.h"

LOG_MODULE_REGISTER(ws_app, LOG_LEVEL_INF);

ZBUS_CHAN_DEFINE(ws_chan_vars, struct ws_msg_vars, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(ws_chan_display, struct ws_msg_display, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(ws_chan_lamp_cmd, struct ws_msg_lamp, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(false));
ZBUS_CHAN_DEFINE(ws_chan_lamp_state, struct ws_msg_lamp, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(false));
ZBUS_CHAN_DEFINE(ws_chan_net, struct ws_msg_net, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(WS_NETST_DOWN));
ZBUS_CHAN_DEFINE(ws_chan_button, struct ws_msg_button, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(ws_chan_cfg, struct ws_msg_cfg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(ws_chan_settings, struct ws_msg_settings, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(ws_chan_mqtt, struct ws_msg_mqtt, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(false));

/* ---- clock ---- */

static int64_t unix_base; /* unix - uptime, 0 before the first sync */
static void clock_tick(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(clock_work, clock_tick); /* static: see display.c */
static bool clock_started;
static int64_t last_sync;
static char sync_server[64];

struct ws_now ws_app_now(void)
{
	int64_t mono = k_uptime_get() / 1000;

	return (struct ws_now){mono, unix_base ? unix_base + mono : 0};
}

void ws_app_set_unix(int64_t unix_s, const char *server)
{
	unix_base = unix_s - k_uptime_get() / 1000;
	last_sync = unix_s;
	snprintf(sync_server, sizeof(sync_server), "%s", server ? server : "");
	if (clock_started) {
		k_work_reschedule(&clock_work, K_NO_WAIT);
	}
}

bool ws_app_time_synced(void)
{
	return unix_base != 0;
}

const char *ws_app_time_server(void)
{
	return sync_server;
}

int64_t ws_app_last_sync(void)
{
	return last_sync;
}

/* ---- device id ---- */

static char dev_id[20];

const char *ws_app_device_id(void)
{
	if (!dev_id[0]) {
		struct ws_settings s;
		uint8_t id[16];
		ssize_t n;

		ws_app_settings_get(&s);
		if (s.dev_id[0]) {
			snprintf(dev_id, sizeof(dev_id), "%s", s.dev_id);
		} else {
			n = hwinfo_get_device_id(id, sizeof(id));
			if (n >= 2) {
				snprintf(dev_id, sizeof(dev_id), "%s_%02x%02x",
					 CONFIG_WS_DEVICE_PREFIX, id[n - 2], id[n - 1]);
			} else {
				snprintf(dev_id, sizeof(dev_id), "%s_0000",
					 CONFIG_WS_DEVICE_PREFIX);
			}
		}
	}
	return dev_id;
}

/* ---- shared state ---- */

K_MUTEX_DEFINE(state_lock);

static struct ws_vars vars;
/* Two compiled configurations: the active one and the one being built. */
static WS_BIG_BSS struct ws_config cfgs[2];
static int active;
static struct ws_engine engine;
static const char *cfg_source = "factory";
static uint32_t cfg_generation;

void ws_app_lock(void)
{
	k_mutex_lock(&state_lock, K_FOREVER);
}

void ws_app_unlock(void)
{
	k_mutex_unlock(&state_lock);
}

struct ws_vars *ws_app_vars(void)
{
	return &vars;
}

struct ws_config *ws_app_cfg(void)
{
	return &cfgs[active];
}

struct ws_engine *ws_app_engine(void)
{
	return &engine;
}

static uint32_t published_seq;

void ws_app_vars_touched(void)
{
	uint32_t seq;

	ws_app_lock();
	seq = vars.seq;
	ws_app_unlock();
	if (seq != published_seq) {
		struct ws_msg_vars m = {seq};

		published_seq = seq;
		zbus_chan_pub(&ws_chan_vars, &m, K_NO_WAIT);
	}
}

void ws_app_set_num(int id, int32_t num)
{
	struct ws_now now = ws_app_now();

	ws_app_lock();
	ws_vars_set_num(&vars, id, num, now.mono);
	ws_app_unlock();
	ws_app_vars_touched();
}

void ws_app_set_str(int id, const char *s)
{
	struct ws_now now = ws_app_now();

	ws_app_lock();
	ws_vars_set_str(&vars, id, s, now.mono);
	ws_app_unlock();
	ws_app_vars_touched();
}

void ws_app_clear(int id)
{
	ws_app_lock();
	ws_vars_clear(&vars, id);
	ws_app_unlock();
	ws_app_vars_touched();
}

/* ---- JSON scratch ---- */

static WS_BIG_BSS char json_buf[WS_MAX_FILE + 1];
static WS_BIG_BSS struct ws_jtok json_toks[WS_JSON_TOKENS];
K_SEM_DEFINE(json_sem, 1, 1);

char *ws_app_json_buf_take(k_timeout_t timeout)
{
	return k_sem_take(&json_sem, timeout) == 0 ? json_buf : NULL;
}

void ws_app_json_buf_give(void)
{
	k_sem_give(&json_sem);
}

/* ---- configuration ---- */

K_MUTEX_DEFINE(cfg_lock); /* serializes compile + swap */

static void apply_ext_ttls(const struct ws_config *c)
{
	for (int i = 0; i < c->n_ext; i++) {
		ws_vars_set_ext_ttl(&vars, c->ext[i].idx, c->ext[i].ttl);
	}
}

static void swap_in(int spare, const char *source)
{
	struct ws_now now = ws_app_now();
	struct ws_msg_cfg m;

	ws_app_lock();
	active = spare;
	ws_cfg_reset_state(&cfgs[active]);
	ws_engine_reconfigure(&engine, &cfgs[active], now.mono);
	apply_ext_ttls(&cfgs[active]);
	cfg_source = source;
	m.generation = ++cfg_generation;
	ws_app_unlock();
	zbus_chan_pub(&ws_chan_cfg, &m, K_MSEC(100));
}

int ws_app_cfg_apply(const char *json, size_t len, bool save, struct ws_cfg_errors *errs)
{
	int r;

	k_mutex_lock(&cfg_lock, K_FOREVER);
	/* The spare buffer is not used by anyone: only this function swaps. */
	int spare = !active;

	r = ws_cfg_compile(json, len, &cfgs[spare], errs, json_toks, WS_JSON_TOKENS);
	if (r == 0 && save) {
		r = ws_storage_save_cfg(json, len);
		if (r < 0) {
			errs->count = 1;
			errs->e[0].path[0] = '\0';
			snprintf(errs->e[0].msg, sizeof(errs->e[0].msg),
				 "не удалось записать файл (%d)", r);
			r = -1;
		}
	}
	if (r == 0) {
		swap_in(spare, save ? "current" : "api");
		LOG_INF("screens applied: %u screens", cfgs[active].n_screens);
	}
	k_mutex_unlock(&cfg_lock);
	return r;
}

static int read_current(void *ctx, char *buf, size_t cap)
{
	return ws_storage_read(WS_CFG_CURRENT, buf, cap);
}

static int read_previous(void *ctx, char *buf, size_t cap)
{
	return ws_storage_read(WS_CFG_PREVIOUS, buf, cap);
}

static int read_factory(void *ctx, char *buf, size_t cap)
{
	size_t n = ws_factory_screens_json_len < cap ? ws_factory_screens_json_len : cap;

	memcpy(buf, ws_factory_screens_json, n);
	return (int)n;
}

int ws_app_cfg_rollback(struct ws_cfg_errors *errs)
{
	int r;

	/* the caller must not hold the JSON buffer */
	if (!ws_app_json_buf_take(K_SECONDS(5))) {
		errs->count = 1;
		errs->e[0].path[0] = '\0';
		snprintf(errs->e[0].msg, sizeof(errs->e[0].msg), "устройство занято");
		return -1;
	}
	k_mutex_lock(&cfg_lock, K_FOREVER);
	r = ws_storage_read(WS_CFG_PREVIOUS, json_buf, WS_MAX_FILE);
	if (r <= 0) {
		errs->count = 1;
		errs->e[0].path[0] = '\0';
		snprintf(errs->e[0].msg, sizeof(errs->e[0].msg), "нет предыдущей версии");
		k_mutex_unlock(&cfg_lock);
		ws_app_json_buf_give();
		return -1;
	}
	int spare = !active;

	r = ws_cfg_compile(json_buf, r, &cfgs[spare], errs, json_toks, WS_JSON_TOKENS);
	if (r == 0) {
		r = ws_storage_rollback();
		if (r == 0) {
			swap_in(spare, "current");
		}
	}
	k_mutex_unlock(&cfg_lock);
	ws_app_json_buf_give();
	return r;
}

const char *ws_app_cfg_source(void)
{
	return cfg_source;
}

int ws_app_cfg_json(char *buf, size_t len)
{
	int r;

	ws_app_lock();
	r = ws_cfg_to_json(&cfgs[active], buf, len);
	ws_app_unlock();
	return r;
}

/* Called once from main() after storage and settings are up. */
int ws_app_state_init(void)
{
	static const struct ws_cfg_source chain[] = {
		{"current", read_current, NULL},
		{"previous", read_previous, NULL},
		{"factory", read_factory, NULL},
	};
	static const char *const names[] = {"current", "previous", "factory"};
	struct ws_cfg_errors errs;
	struct ws_now now = ws_app_now();

	ws_vars_init(&vars);
	k_mutex_lock(&cfg_lock, K_FOREVER);
	int used = ws_cfg_load_chain(chain, 3, json_buf, WS_MAX_FILE, &cfgs[0], &errs, json_toks,
				     WS_JSON_TOKENS);

	if (used < 0) {
		/* cannot happen with a valid factory set; keep an empty default */
		LOG_ERR("no usable screen configuration");
		used = 2;
	} else if (used > 0) {
		LOG_WRN("screens.json unusable, loaded %s set", names[used]);
	}
	active = 0;
	cfg_source = names[used];
	ws_engine_init(&engine, &cfgs[0], now.mono);
	apply_ext_ttls(&cfgs[0]);
	k_mutex_unlock(&cfg_lock);
	LOG_INF("screens: %u from %s", cfgs[0].n_screens, cfg_source);
	return 0;
}

/* ---- local time ---- */

static struct ws_tz tz;
static char tz_str[48];

bool ws_app_localtime(struct ws_tm *tm)
{
	struct ws_now now = ws_app_now();
	struct ws_settings s;

	if (!now.unix_s) {
		return false;
	}
	ws_app_settings_get(&s);
	if (strcmp(s.tz, tz_str) != 0) {
		if (ws_tz_parse(s.tz, &tz) < 0) {
			ws_tz_parse("MSK-3", &tz);
		}
		snprintf(tz_str, sizeof(tz_str), "%s", s.tz);
	}
	ws_localtime(&tz, now.unix_s, tm);
	return true;
}

/* time.hour / time.min / time.dow and sun.up, once a minute (and right after
 * a sync, see ws_app_set_unix) */

static void clock_tick(struct k_work *w)
{
	struct ws_tm tm;
	struct ws_settings s;
	struct ws_now now = ws_app_now();

	if (ws_app_localtime(&tm)) {
		ws_app_lock();
		ws_vars_set_num(&vars, WS_V_TIME_HOUR, tm.hour * 10, now.mono);
		ws_vars_set_num(&vars, WS_V_TIME_MIN, tm.min * 10, now.mono);
		ws_vars_set_num(&vars, WS_V_TIME_DOW, tm.dow * 10, now.mono);
		ws_app_settings_get(&s);
		if (s.lat_e6 || s.lon_e6) {
			bool up = ws_sun_up(s.lat_e6 / 1e6, s.lon_e6 / 1e6, now.unix_s);

			ws_vars_set_num(&vars, WS_V_SUN_UP, up, now.mono);
		}
		ws_app_unlock();
		ws_app_vars_touched();
		/* next tick right after the minute changes */
		k_work_reschedule(&clock_work, K_MSEC((60 - tm.sec) * 1000 + 20));
		return;
	}
	k_work_reschedule(&clock_work, K_SECONDS(5));
}

void ws_app_clock_start(void)
{
	clock_started = true;
	k_work_reschedule(&clock_work, K_NO_WAIT);
}
