/*
 * Shared application state and zbus channels.
 *
 * The decision logic lives in fw/lib (pure C); services in fw/src only move
 * data between it and the outside world. Variables, the compiled screen
 * configuration and the screen engine are shared state guarded by one
 * mutex; changes are announced on zbus channels.
 */
#ifndef WS_APP_H_
#define WS_APP_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include <ws/config.h>
#include <ws/screens.h>
#include <ws/settings.h>
#include <ws/tz.h>
#include <ws/vars.h>

/* ---- clock ---- */

/* Monotonic seconds and unix time (0 until the first NTP sync). */
struct ws_now ws_app_now(void);
void ws_app_set_unix(int64_t unix_s, const char *server);
bool ws_app_time_synced(void);
const char *ws_app_time_server(void);
int64_t ws_app_last_sync(void); /* unix time of the last sync, 0 never */
/* Local time by the POSIX TZ from the settings; false until synced. */
bool ws_app_localtime(struct ws_tm *tm);
/* Starts the minute ticker that keeps time.* and sun.up current. */
void ws_app_clock_start(void);
int ws_app_state_init(void);

/* ---- device ---- */

const char *ws_app_device_id(void); /* "ws_a1b2" */

/* ---- shared state ---- */

void ws_app_lock(void);
void ws_app_unlock(void);

/* All three need the lock. */
struct ws_vars *ws_app_vars(void);
struct ws_config *ws_app_cfg(void);
struct ws_engine *ws_app_engine(void);

/* Convenience setters: lock, set, unlock and notify on change. */
void ws_app_set_num(int id, int32_t num);
void ws_app_set_str(int id, const char *s);
void ws_app_clear(int id);
/* Announces "variables changed" (ws_chan_vars) if the store changed. */
void ws_app_vars_touched(void);

/* Validates, compiles and swaps in a new screen configuration. When `save`
 * is set the JSON is written to flash (current -> previous). Returns 0 or
 * -1 with `errs`. Safe to call from any thread. */
int ws_app_cfg_apply(const char *json, size_t len, bool save, struct ws_cfg_errors *errs);
/* Restores the previous configuration file. */
int ws_app_cfg_rollback(struct ws_cfg_errors *errs);
/* Where the active configuration came from: "current", "previous",
 * "factory" or "api". */
const char *ws_app_cfg_source(void);
/* Canonical JSON of the active configuration into a static buffer (holds
 * the lock while serializing). Returns length or -1. */
int ws_app_cfg_json(char *buf, size_t len);

/* Scratch buffers for JSON work (32 KB text + tokens); one user at a time. */
char *ws_app_json_buf_take(k_timeout_t timeout);
void ws_app_json_buf_give(void);
struct ws_jtok *ws_app_json_toks(void);

/* ---- settings ---- */

/* Copy of the device settings (no lock needed by the caller). */
void ws_app_settings_get(struct ws_settings *out);
/* Applies a JSON patch and stores changed fields; announces ws_chan_settings. */
int ws_app_settings_apply(const char *json, size_t len, struct ws_cfg_errors *errs);
/* Single field update from code (e.g. lamp.last). */
void ws_app_settings_set_int(const char *key, int32_t v);

/* ---- zbus channels ---- */

struct ws_msg_vars {
	uint32_t seq;
};

struct ws_msg_display {
	char screen[WS_ID_LEN];
	char name[WS_NAME_LEN];
	char reason[96];
	bool pinned;
	uint32_t flips_24h;
};

struct ws_msg_lamp {
	bool on;
};

enum ws_net_status {
	WS_NETST_DOWN,
	WS_NETST_CONNECTING,
	WS_NETST_ONLINE,
	WS_NETST_AP,
};

struct ws_msg_net {
	uint8_t status; /* enum ws_net_status */
	int8_t rssi;
	char ip[16];
};

enum ws_btn_msg {
	WS_BTNMSG_SHORT,
	WS_BTNMSG_LONG,
	WS_BTNMSG_BOOT,
};

struct ws_msg_button {
	uint8_t event; /* enum ws_btn_msg */
};

struct ws_msg_cfg {
	uint32_t generation;
};

struct ws_msg_settings {
	uint64_t changed; /* bit per ws_setting_fields index */
};

struct ws_msg_mqtt {
	bool connected;
};

ZBUS_CHAN_DECLARE(ws_chan_vars, ws_chan_display, ws_chan_lamp_cmd, ws_chan_lamp_state, ws_chan_net,
		  ws_chan_button, ws_chan_cfg, ws_chan_settings, ws_chan_mqtt);

#endif /* WS_APP_H_ */
