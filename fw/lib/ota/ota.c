/* Firmware update state machine, see ws/ota.h. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <ws/json.h>
#include <ws/ota.h>
#include <ws/util.h>

static void enter(struct ws_ota *o, enum ws_ota_state s, int64_t now)
{
	o->state = s;
	o->since = now;
	o->deadline = 0;
}

static enum ws_ota_action fail(struct ws_ota *o, const char *why, int64_t now)
{
	snprintf(o->error, sizeof(o->error), "%s", why);
	enter(o, WS_OTA_FAILED, now);
	return WS_OTA_ACT_NONE;
}

void ws_ota_init(struct ws_ota *o)
{
	memset(o, 0, sizeof(*o));
	o->confirmed = true;
	o->selftest_s = WS_OTA_SELFTEST_S;
}

static bool busy(const struct ws_ota *o)
{
	return o->state == WS_OTA_DOWNLOAD || o->state == WS_OTA_VERIFY ||
	       o->state == WS_OTA_READY || o->state == WS_OTA_TESTING;
}

static void start(struct ws_ota *o, enum ws_ota_source src, int64_t now)
{
	o->source = src;
	o->size = 0;
	o->error[0] = '\0';
	enter(o, WS_OTA_DOWNLOAD, now);
}

enum ws_ota_action ws_ota_request(struct ws_ota *o, const char *json, size_t len, int64_t now)
{
	struct ws_jtok toks[16];
	struct ws_json j;
	char sha[65];
	struct ws_url u;

	if (busy(o)) {
		snprintf(o->error, sizeof(o->error), "занято: %s", ws_ota_state_name(o->state));
		return WS_OTA_ACT_NONE;
	}
	if (ws_json_parse(&j, json, len, toks, 16) < 0 || !ws_json_is(&j, 0, WS_J_OBJ)) {
		return fail(o, "нужен JSON {url, sha256, version}", now);
	}
	if (ws_json_str(&j, ws_json_get(&j, 0, "url"), o->url, sizeof(o->url)) <= 0 ||
	    ws_url_parse(o->url, &u)) {
		return fail(o, "неверный url", now);
	}
	if (ws_json_str(&j, ws_json_get(&j, 0, "sha256"), sha, sizeof(sha)) != 64 ||
	    ws_unhex(sha, o->sha256, 32)) {
		return fail(o, "sha256: 64 шестнадцатеричных знака", now);
	}
	o->check_sha = true;
	o->version[0] = '\0';
	ws_json_str(&j, ws_json_get(&j, 0, "version"), o->version, sizeof(o->version));
	o->total = 0;
	start(o, WS_OTA_SRC_MQTT, now);
	return WS_OTA_ACT_DOWNLOAD;
}

enum ws_ota_action ws_ota_upload_begin(struct ws_ota *o, enum ws_ota_source src, size_t total,
				       const uint8_t sha256[32], int64_t now)
{
	if (busy(o)) {
		snprintf(o->error, sizeof(o->error), "занято: %s", ws_ota_state_name(o->state));
		return WS_OTA_ACT_NONE;
	}
	if (total == 0) {
		return fail(o, "пустой образ", now);
	}
	o->url[0] = '\0';
	o->version[0] = '\0';
	o->total = total;
	o->check_sha = sha256 != NULL; /* without one MCUboot checks the signature */
	if (sha256) {
		memcpy(o->sha256, sha256, 32);
	} else {
		memset(o->sha256, 0, 32);
	}
	start(o, src, now);
	return WS_OTA_ACT_NONE;
}

enum ws_ota_action ws_ota_progress(struct ws_ota *o, size_t len)
{
	if (o->state != WS_OTA_DOWNLOAD) {
		return WS_OTA_ACT_NONE;
	}
	o->size += len;
	if (o->total && o->size > o->total) {
		snprintf(o->error, sizeof(o->error), "образ длиннее заявленного");
		o->state = WS_OTA_FAILED;
		return WS_OTA_ACT_NONE;
	}
	if (o->total && o->size == o->total) {
		o->state = WS_OTA_VERIFY;
		return WS_OTA_ACT_VERIFY;
	}
	return WS_OTA_ACT_NONE;
}

enum ws_ota_action ws_ota_download_done(struct ws_ota *o, int err, int64_t now)
{
	if (o->state == WS_OTA_VERIFY && !err) {
		return WS_OTA_ACT_NONE; /* already complete by size */
	}
	if (o->state != WS_OTA_DOWNLOAD) {
		return WS_OTA_ACT_NONE;
	}
	if (err) {
		char why[64];

		snprintf(why, sizeof(why), "загрузка: ошибка %d", err);
		return fail(o, why, now);
	}
	if (o->size == 0) {
		return fail(o, "пустой образ", now);
	}
	if (o->total && o->size != o->total) {
		return fail(o, "образ короче заявленного", now);
	}
	enter(o, WS_OTA_VERIFY, now);
	return WS_OTA_ACT_VERIFY;
}

enum ws_ota_action ws_ota_verified(struct ws_ota *o, const uint8_t sha256[32], int64_t now)
{
	if (o->state != WS_OTA_VERIFY) {
		return WS_OTA_ACT_NONE;
	}
	if (o->check_sha && !ws_ct_equal(o->sha256, sha256, 32)) {
		return fail(o, "sha256 не совпадает", now);
	}
	enter(o, WS_OTA_READY, now);
	return WS_OTA_ACT_MARK;
}

enum ws_ota_action ws_ota_marked(struct ws_ota *o, int err, int64_t now)
{
	if (o->state != WS_OTA_READY) {
		return WS_OTA_ACT_NONE;
	}
	if (err) {
		return fail(o, "не удалось отметить слот", now);
	}
	o->deadline = now + WS_OTA_REBOOT_DELAY;
	return WS_OTA_ACT_NONE;
}

enum ws_ota_action ws_ota_boot(struct ws_ota *o, bool confirmed, int64_t now)
{
	o->confirmed = confirmed;
	if (confirmed) {
		return WS_OTA_ACT_NONE;
	}
	enter(o, WS_OTA_TESTING, now);
	o->deadline = now + o->selftest_s;
	return WS_OTA_ACT_NONE;
}

enum ws_ota_action ws_ota_mqtt_connected(struct ws_ota *o, int64_t now)
{
	if (o->state != WS_OTA_TESTING) {
		return WS_OTA_ACT_NONE;
	}
	o->confirmed = true;
	enter(o, WS_OTA_IDLE, now);
	return WS_OTA_ACT_CONFIRM;
}

enum ws_ota_action ws_ota_tick(struct ws_ota *o, int64_t now)
{
	if (!o->deadline || now < o->deadline) {
		return WS_OTA_ACT_NONE;
	}
	switch (o->state) {
	case WS_OTA_READY:
		o->deadline = 0;
		return WS_OTA_ACT_REBOOT;
	case WS_OTA_TESTING:
		/* no broker in time: reboot unconfirmed, MCUboot takes the old image */
		snprintf(o->error, sizeof(o->error), "самопроверка не пройдена");
		o->deadline = 0;
		return WS_OTA_ACT_REBOOT;
	default:
		return WS_OTA_ACT_NONE;
	}
}

const char *ws_ota_state_name(enum ws_ota_state s)
{
	static const char *const n[] = {"idle", "download", "verify", "ready", "testing", "failed"};

	return (unsigned)s < sizeof(n) / sizeof(n[0]) ? n[s] : "?";
}

int ws_ota_percent(const struct ws_ota *o)
{
	if (!o->total) {
		return -1;
	}
	return (int)((uint64_t)o->size * 100 / o->total);
}
