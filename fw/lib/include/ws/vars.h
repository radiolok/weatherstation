/*
 * Variable store of the screen constructor (docs/screen-constructor.md, 3).
 *
 * Fixed table of variables with type, unit, update time and lifetime. Numbers
 * are kept in tenths (deci-units) so that comparisons and rounding are exact
 * and identical to the browser renderer. A variable without a value or older
 * than its lifetime is unknown.
 *
 * Reading applies the METAR fallback: when the forecast is older than
 * 120 min and the METAR report is not older than 90 min, out.* and fc.ice
 * come from obs.*, and forecast-only fc.* become unknown.
 */
#ifndef WS_VARS_H_
#define WS_VARS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ws_var_id {
	WS_V_OUT_T,
	WS_V_OUT_COND,
	WS_V_OUT_WIND,
	WS_V_OUT_WIND_DIR,
	WS_V_OUT_RH,
	WS_V_OUT_P,
	WS_V_FC_TMAX,
	WS_V_FC_TMIN,
	WS_V_FC_RAIN_FROM,
	WS_V_FC_RAIN_TO,
	WS_V_FC_RAIN_IN,
	WS_V_FC_SNOW,
	WS_V_FC_ICE,
	WS_V_FC_STORM,
	WS_V_FC_NEXT_NAME,
	WS_V_FC_NEXT_T,
	WS_V_FC_NEXT_COND,
	WS_V_FC_HOURS,
	WS_V_FC_AGE,
	WS_V_IN_T,
	WS_V_IN_RH,
	WS_V_IN_P,
	WS_V_IN_P_TREND,
	WS_V_IN_CO2,
	WS_V_TIME_HOUR,
	WS_V_TIME_MIN,
	WS_V_TIME_DOW,
	WS_V_SUN_UP,
	WS_V_SYS_LAMP,
	WS_V_SYS_MQTT,
	WS_V_SYS_WIFI_RSSI,
	WS_V_EXT1,
	WS_V_EXT2,
	WS_V_EXT3,
	WS_V_EXT4,
	WS_V_EXT5,
	WS_V_EXT6,
	WS_V_EXT7,
	WS_V_EXT8,
	WS_V_OBS_T,
	WS_V_OBS_COND,
	WS_V_OBS_WIND,
	WS_V_OBS_WIND_DIR,
	WS_V_OBS_P,
	WS_V_OBS_RH,
	WS_V_OBS_ICE,
	WS_V_OBS_AGE,
	WS_V_COUNT
};

#define WS_V_NONE 0xFF

enum ws_vtype {
	WS_VT_NUM,   /* number in tenths */
	WS_VT_BOOL,  /* 0 / 1 */
	WS_VT_COND,  /* enum ws_cond */
	WS_VT_DIR,   /* enum ws_dir, where the wind comes from */
	WS_VT_STR,   /* short string */
	WS_VT_HOURS, /* fc.hours: up to 12 (t, pop) pairs */
	WS_VT_ANY,   /* ext.*: number or string, decided by the value */
};

enum ws_vsrc {
	WS_SRC_NONE,
	WS_SRC_FORECAST,
	WS_SRC_METAR,
	WS_SRC_SENSOR,
	WS_SRC_CLOCK,
	WS_SRC_SYSTEM,
	WS_SRC_MQTT,
};

struct ws_var_info {
	const char *name;
	uint8_t type; /* enum ws_vtype */
	uint8_t src;  /* enum ws_vsrc */
	const char *unit;
	uint32_t ttl_s; /* 0: never expires */
};

#define WS_VAR_STR_LEN 24
#define WS_HOURS_MAX   12
#define WS_AGE_NEVER   99999 /* fc.age / obs.age when nothing was received */
#define WS_RX_NEVER    INT64_MIN

/* Forecast older than this switches to METAR, if METAR is fresh enough. */
#define WS_FALLBACK_FC_AGE_MIN  120
#define WS_FALLBACK_OBS_AGE_MIN 90

struct ws_hours {
	uint8_t n;
	int16_t t[WS_HOURS_MAX]; /* deg C, integer */
	uint8_t pop[WS_HOURS_MAX];
};

struct ws_value {
	bool known;
	uint8_t type; /* enum ws_vtype of the value (number or string for ext.*) */
	uint8_t src;  /* enum ws_vsrc actually used (after fallback) */
	int32_t num;
	const char *str;              /* WS_VT_STR */
	const struct ws_hours *hours; /* WS_VT_HOURS */
};

struct ws_var_slot {
	bool set;
	bool is_str;
	int32_t num;
	int64_t updated; /* monotonic seconds */
	char str[WS_VAR_STR_LEN];
};

struct ws_vars {
	struct ws_var_slot v[WS_V_COUNT];
	struct ws_hours hours;
	uint32_t ext_ttl[8];
	int64_t fc_rx;  /* monotonic s of the last forecast, WS_RX_NEVER if never */
	int64_t fc_ts;  /* unix time inside the forecast, 0 unknown */
	int64_t obs_ts; /* unix time of the METAR observation, 0 unknown */
	int64_t obs_rx; /* monotonic s of the last METAR, WS_RX_NEVER if never */
	uint32_t seq;   /* incremented on every change */
};

/* Wall clock for age calculations: unix_s is 0 until the first NTP sync. */
struct ws_now {
	int64_t mono;
	int64_t unix_s;
};

const struct ws_var_info *ws_var_info(int id);
/* Variable id by name ("out.t", "fc.next" is an alias of fc.next_cond),
 * -1 if unknown. */
int ws_var_lookup(const char *name);
const char *ws_var_name(int id);
const char *ws_vsrc_name(int src);

void ws_vars_init(struct ws_vars *vars);
void ws_vars_set_num(struct ws_vars *vars, int id, int32_t num, int64_t mono);
void ws_vars_set_str(struct ws_vars *vars, int id, const char *s, int64_t mono);
void ws_vars_clear(struct ws_vars *vars, int id);
void ws_vars_set_hours(struct ws_vars *vars, const struct ws_hours *h, int64_t mono);
void ws_vars_set_ext_ttl(struct ws_vars *vars, int ext_idx, uint32_t ttl_s);

/* METAR report received: observation time (unix, 0 if unknown). */
void ws_vars_set_obs_time(struct ws_vars *vars, int64_t obs_unix, int64_t mono);

/* Effective value at `now`, with lifetime and METAR fallback applied. */
bool ws_vars_get(const struct ws_vars *vars, int id, const struct ws_now *now,
		 struct ws_value *out);
/* True when out.* currently come from METAR. */
bool ws_vars_fallback_active(const struct ws_vars *vars, const struct ws_now *now);
/* Minutes since the forecast / METAR, WS_AGE_NEVER if never received. */
int32_t ws_vars_fc_age(const struct ws_vars *vars, const struct ws_now *now);
int32_t ws_vars_obs_age(const struct ws_vars *vars, const struct ws_now *now);

/* Formats a value for display and JSON ("12.5", "rain", "true", "--"). */
int ws_value_format(const struct ws_value *v, int var_type, char *buf, size_t len);

/* Forecast from the server (ws/<id>/forecast), see fw/README.md. Unknown or
 * missing fields clear the corresponding variables. Returns 0 or -1 for
 * invalid JSON. */
int ws_forecast_apply(struct ws_vars *vars, const char *json, size_t len, int64_t mono);

/* External variable ext.<idx 1..8> from an MQTT payload: `field` is a dotted
 * path into a JSON object ("" means the payload itself is the value). */
int ws_ext_apply(struct ws_vars *vars, int ext_idx, const char *field, const char *payload,
		 size_t len, int64_t mono);

struct ws_json;

/* /api/vars: {"fallback": false, "vars": [{"name": "out.t", "value": -2,
 * "unit": "°C", "src": "forecast", "age": 120, "type": "num"}, ...]}
 * Values in natural units, unknown values are null. */
int ws_vars_to_json(const struct ws_vars *vars, const struct ws_now *now, char *buf, size_t len);

/* Sets variables from a JSON object {"out.t": -2, "out.cond": "rain",
 * "fc.ice": true, "fc.hours": [[t, pop], ...], "in.co2": null} (null makes
 * a variable unknown). Used by /api/render and the simulator. Unknown names
 * are ignored; returns the number of variables set. */
int ws_vars_apply_json(struct ws_vars *vars, const struct ws_json *j, int obj, int64_t mono);

#ifdef __cplusplus
}
#endif

#endif /* WS_VARS_H_ */
