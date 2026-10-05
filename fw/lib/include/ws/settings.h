/*
 * Device settings (Wi-Fi, MQTT, NTP, METAR, location, web password, lamp,
 * sensor offsets): one table drives storage keys, the JSON of the settings
 * page and validation. Secrets are write-only through JSON.
 */
#ifndef WS_SETTINGS_H_
#define WS_SETTINGS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ws/config.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ws_lamp_restore {
	WS_LAMP_RESTORE_OFF,
	WS_LAMP_RESTORE_ON,
	WS_LAMP_RESTORE_LAST,
};

struct ws_settings {
	char wifi_ssid[33];
	char wifi_psk[65];
	char mqtt_host[64];
	int32_t mqtt_port;
	char mqtt_user[33];
	char mqtt_pass[65];
	char ntp1[64];
	char ntp2[64];
	int32_t ntp_interval; /* minutes */
	char tz[48];
	char metar_icao[5];
	char metar_url1[128];
	char metar_url2[128];
	int32_t metar_period;   /* minutes */
	int32_t lat_e6, lon_e6; /* micro-degrees, 0/0 = not set */
	char web_user[17];
	char web_hash[65]; /* sha256(salt + password), hex */
	char web_salt[17];
	int32_t lamp_restore;
	int32_t lamp_last;
	int32_t t_offset;  /* tenths of degC */
	int32_t rh_offset; /* tenths of %RH */
	char dev_id[16];   /* empty: ws_<last 4 hex of the MAC> */
	char ca_pem[2048]; /* root CA for METAR HTTPS, empty: built-in */
};

enum ws_set_type {
	WS_SET_STR,
	WS_SET_INT,
	WS_SET_ENUM,
};

struct ws_setting_field {
	const char *key; /* storage key under "ws/" and JSON key */
	uint8_t type;
	bool secret; /* never returned, only "<key>_set": true/false */
	bool reboot; /* takes effect after a reconnect */
	uint16_t offset;
	uint16_t size;
	int32_t min, max;
	const char *const *enums;
};

extern const struct ws_setting_field ws_setting_fields[];
extern const size_t ws_setting_field_count;

void ws_settings_defaults(struct ws_settings *s);
const struct ws_setting_field *ws_setting_find(const char *key);
void *ws_setting_ptr(struct ws_settings *s, const struct ws_setting_field *f);

/* JSON for the settings page; secrets are replaced by "<key>_set". */
int ws_settings_to_json(const struct ws_settings *s, char *buf, size_t len);

/* Applies a partial JSON object onto `s` after validating every field; on
 * error nothing is changed. A "web.password" key sets the password: the
 * caller provides a fresh random salt. `changed` gets a bit per field index
 * that was modified (fields <= 64). Returns 0 or -1 with errors. */
int ws_settings_apply_json(struct ws_settings *s, const char *json, size_t len,
			   const char *new_salt, uint64_t *changed, struct ws_cfg_errors *errs);

/* Password check for HTTP Basic Auth. Empty hash: any password works only
 * in access point mode, the caller decides. */
bool ws_settings_check_password(const struct ws_settings *s, const char *password);
void ws_settings_set_password(struct ws_settings *s, const char *password, const char *salt);

/* "https://.../{icao}.TXT" with {icao} replaced */
int ws_settings_metar_url(const struct ws_settings *s, int which, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
