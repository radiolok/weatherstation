/*
 * MQTT topics and payloads, Home Assistant discovery (fw/README.md, MQTT).
 * Only formatting and parsing: the mqtt service does the I/O.
 */
#ifndef WS_HA_H_
#define WS_HA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ws/config.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS_HA_PREFIX "homeassistant"
#define WS_AUTO_RU   "\xD0\x90\xD0\xB2\xD1\x82\xD0\xBE" /* "Авто" */

struct ws_ha_device {
	const char *id;      /* "ws_a1b2" */
	const char *version; /* firmware version */
};

enum ws_ha_entity {
	WS_HA_TEMP,
	WS_HA_HUMIDITY,
	WS_HA_PRESSURE,
	WS_HA_CO2,
	WS_HA_RSSI,
	WS_HA_LAMP,
	WS_HA_SCREEN,
	WS_HA_REASON,
	WS_HA_ENTITY_COUNT
};

/* "ws/<id>/<sub>" */
int ws_topic(char *buf, size_t len, const char *id, const char *sub);

/* Discovery topic and retained config payload of one entity. The screen
 * select lists "Авто" and the screen names of `cfg`. */
int ws_ha_discovery(const struct ws_ha_device *dev, enum ws_ha_entity e,
		    const struct ws_config *cfg, char *topic, size_t topic_len, char *payload,
		    size_t payload_len);

struct ws_sensor_report {
	bool t_ok, rh_ok, p_ok, trend_ok, co2_ok, rssi_ok;
	int32_t t, rh, p, trend; /* tenths */
	int32_t co2;             /* ppm */
	int32_t rssi;            /* dBm */
};

/* {"t":22.4,"rh":41,"p":747.5,"ptrend":-0.5,"co2":640,"rssi":-61}, unknown
 * values are null */
int ws_sensors_json(const struct ws_sensor_report *r, char *buf, size_t len);

/* {"screen":"main","name":"Обычный","reason":"...","pinned":false,"flips_24h":123} */
int ws_display_state_json(const char *id, const char *name, const char *reason, bool pinned,
			  uint32_t flips_24h, char *buf, size_t len);

/* "ON" / "OFF" (also on/off/1/0/true/false) -> 1 / 0, -1 if unknown */
int ws_parse_onoff(const char *payload, size_t len);

/* display/pin: "auto", "Авто", {"screen": "rain", "minutes": 30},
 * {"screen": null}, a screen id or a screen name. `screen` = -1 means auto.
 * Returns 0 or -1. */
int ws_parse_pin(const struct ws_config *cfg, const char *payload, size_t len, int *screen,
		 uint32_t *minutes);

#ifdef __cplusplus
}
#endif

#endif
