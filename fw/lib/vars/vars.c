/* Variable store, see ws/vars.h. */
#include <stdio.h>
#include <string.h>

#include <ws/json.h>
#include <ws/sign.h>
#include <ws/vars.h>

#define H3   (3 * 3600)
#define SENS 120

static const struct ws_var_info infos[WS_V_COUNT] = {
	[WS_V_OUT_T] = {"out.t", WS_VT_NUM, WS_SRC_FORECAST, "°C", H3},
	[WS_V_OUT_COND] = {"out.cond", WS_VT_COND, WS_SRC_FORECAST, "", H3},
	[WS_V_OUT_WIND] = {"out.wind", WS_VT_NUM, WS_SRC_FORECAST, "м/с", H3},
	[WS_V_OUT_WIND_DIR] = {"out.wind_dir", WS_VT_DIR, WS_SRC_FORECAST, "", H3},
	[WS_V_OUT_RH] = {"out.rh", WS_VT_NUM, WS_SRC_FORECAST, "%", H3},
	[WS_V_OUT_P] = {"out.p", WS_VT_NUM, WS_SRC_FORECAST, "мм", H3},
	[WS_V_FC_TMAX] = {"fc.tmax", WS_VT_NUM, WS_SRC_FORECAST, "°C", H3},
	[WS_V_FC_TMIN] = {"fc.tmin", WS_VT_NUM, WS_SRC_FORECAST, "°C", H3},
	[WS_V_FC_RAIN_FROM] = {"fc.rain_from", WS_VT_NUM, WS_SRC_FORECAST, "ч", H3},
	[WS_V_FC_RAIN_TO] = {"fc.rain_to", WS_VT_NUM, WS_SRC_FORECAST, "ч", H3},
	[WS_V_FC_RAIN_IN] = {"fc.rain_in", WS_VT_NUM, WS_SRC_FORECAST, "мин", H3},
	[WS_V_FC_SNOW] = {"fc.snow", WS_VT_BOOL, WS_SRC_FORECAST, "", H3},
	[WS_V_FC_ICE] = {"fc.ice", WS_VT_BOOL, WS_SRC_FORECAST, "", H3},
	[WS_V_FC_STORM] = {"fc.storm", WS_VT_BOOL, WS_SRC_FORECAST, "", H3},
	[WS_V_FC_NEXT_NAME] = {"fc.next_name", WS_VT_STR, WS_SRC_FORECAST, "", H3},
	[WS_V_FC_NEXT_T] = {"fc.next_t", WS_VT_NUM, WS_SRC_FORECAST, "°C", H3},
	[WS_V_FC_NEXT_COND] = {"fc.next_cond", WS_VT_COND, WS_SRC_FORECAST, "", H3},
	[WS_V_FC_HOURS] = {"fc.hours", WS_VT_HOURS, WS_SRC_FORECAST, "", H3},
	[WS_V_FC_AGE] = {"fc.age", WS_VT_NUM, WS_SRC_FORECAST, "мин", 0},
	[WS_V_IN_T] = {"in.t", WS_VT_NUM, WS_SRC_SENSOR, "°C", SENS},
	[WS_V_IN_RH] = {"in.rh", WS_VT_NUM, WS_SRC_SENSOR, "%", SENS},
	[WS_V_IN_P] = {"in.p", WS_VT_NUM, WS_SRC_SENSOR, "мм", SENS},
	[WS_V_IN_P_TREND] = {"in.p_trend", WS_VT_NUM, WS_SRC_SENSOR, "мм", 1200},
	[WS_V_IN_CO2] = {"in.co2", WS_VT_NUM, WS_SRC_SENSOR, "ppm", SENS},
	[WS_V_TIME_HOUR] = {"time.hour", WS_VT_NUM, WS_SRC_CLOCK, "ч", 0},
	[WS_V_TIME_MIN] = {"time.min", WS_VT_NUM, WS_SRC_CLOCK, "мин", 0},
	[WS_V_TIME_DOW] = {"time.dow", WS_VT_NUM, WS_SRC_CLOCK, "", 0},
	[WS_V_SUN_UP] = {"sun.up", WS_VT_BOOL, WS_SRC_CLOCK, "", 0},
	[WS_V_SYS_LAMP] = {"sys.lamp", WS_VT_BOOL, WS_SRC_SYSTEM, "", 0},
	[WS_V_SYS_MQTT] = {"sys.mqtt", WS_VT_BOOL, WS_SRC_SYSTEM, "", 0},
	[WS_V_SYS_WIFI_RSSI] = {"sys.wifi_rssi", WS_VT_NUM, WS_SRC_SYSTEM, "дБм", 0},
	[WS_V_EXT1] = {"ext.1", WS_VT_ANY, WS_SRC_MQTT, "", 0},
	[WS_V_EXT2] = {"ext.2", WS_VT_ANY, WS_SRC_MQTT, "", 0},
	[WS_V_EXT3] = {"ext.3", WS_VT_ANY, WS_SRC_MQTT, "", 0},
	[WS_V_EXT4] = {"ext.4", WS_VT_ANY, WS_SRC_MQTT, "", 0},
	[WS_V_EXT5] = {"ext.5", WS_VT_ANY, WS_SRC_MQTT, "", 0},
	[WS_V_EXT6] = {"ext.6", WS_VT_ANY, WS_SRC_MQTT, "", 0},
	[WS_V_EXT7] = {"ext.7", WS_VT_ANY, WS_SRC_MQTT, "", 0},
	[WS_V_EXT8] = {"ext.8", WS_VT_ANY, WS_SRC_MQTT, "", 0},
	[WS_V_OBS_T] = {"obs.t", WS_VT_NUM, WS_SRC_METAR, "°C", H3},
	[WS_V_OBS_COND] = {"obs.cond", WS_VT_COND, WS_SRC_METAR, "", H3},
	[WS_V_OBS_WIND] = {"obs.wind", WS_VT_NUM, WS_SRC_METAR, "м/с", H3},
	[WS_V_OBS_WIND_DIR] = {"obs.wind_dir", WS_VT_DIR, WS_SRC_METAR, "", H3},
	[WS_V_OBS_P] = {"obs.p", WS_VT_NUM, WS_SRC_METAR, "мм", H3},
	[WS_V_OBS_RH] = {"obs.rh", WS_VT_NUM, WS_SRC_METAR, "%", H3},
	[WS_V_OBS_ICE] = {"obs.ice", WS_VT_BOOL, WS_SRC_METAR, "", H3},
	[WS_V_OBS_AGE] = {"obs.age", WS_VT_NUM, WS_SRC_METAR, "мин", 0},
};

static const char *const src_names[] = {"none",  "forecast", "metar", "sensor",
					"clock", "system",   "mqtt"};

const struct ws_var_info *ws_var_info(int id)
{
	return id >= 0 && id < WS_V_COUNT ? &infos[id] : NULL;
}

int ws_var_lookup(const char *name)
{
	if (strcmp(name, "fc.next") == 0) {
		return WS_V_FC_NEXT_COND;
	}
	for (int i = 0; i < WS_V_COUNT; i++) {
		if (strcmp(infos[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

const char *ws_var_name(int id)
{
	return id >= 0 && id < WS_V_COUNT ? infos[id].name : "";
}

const char *ws_vsrc_name(int src)
{
	return src >= 0 && src < (int)(sizeof(src_names) / sizeof(src_names[0])) ? src_names[src]
										 : "";
}

void ws_vars_init(struct ws_vars *vars)
{
	memset(vars, 0, sizeof(*vars));
	vars->fc_rx = WS_RX_NEVER;
	vars->obs_rx = WS_RX_NEVER;
}

static void touch(struct ws_vars *vars)
{
	vars->seq++;
}

void ws_vars_set_num(struct ws_vars *vars, int id, int32_t num, int64_t mono)
{
	if (id < 0 || id >= WS_V_COUNT) {
		return;
	}
	struct ws_var_slot *s = &vars->v[id];

	if (!s->set || s->is_str || s->num != num) {
		touch(vars);
	}
	s->set = true;
	s->is_str = false;
	s->num = num;
	s->updated = mono;
}

void ws_vars_set_str(struct ws_vars *vars, int id, const char *str, int64_t mono)
{
	if (id < 0 || id >= WS_V_COUNT) {
		return;
	}
	struct ws_var_slot *s = &vars->v[id];

	if (!s->set || !s->is_str || strncmp(s->str, str, sizeof(s->str) - 1) != 0) {
		touch(vars);
	}
	s->set = true;
	s->is_str = true;
	strncpy(s->str, str, sizeof(s->str) - 1);
	s->str[sizeof(s->str) - 1] = '\0';
	s->updated = mono;
}

void ws_vars_clear(struct ws_vars *vars, int id)
{
	if (id < 0 || id >= WS_V_COUNT) {
		return;
	}
	if (vars->v[id].set) {
		touch(vars);
	}
	vars->v[id].set = false;
	if (id == WS_V_FC_HOURS) {
		vars->hours.n = 0;
	}
}

void ws_vars_set_hours(struct ws_vars *vars, const struct ws_hours *h, int64_t mono)
{
	if (memcmp(&vars->hours, h, sizeof(*h)) != 0 || !vars->v[WS_V_FC_HOURS].set) {
		touch(vars);
	}
	vars->hours = *h;
	vars->v[WS_V_FC_HOURS].set = h->n > 0;
	vars->v[WS_V_FC_HOURS].updated = mono;
}

void ws_vars_set_ext_ttl(struct ws_vars *vars, int ext_idx, uint32_t ttl_s)
{
	if (ext_idx >= 1 && ext_idx <= 8) {
		vars->ext_ttl[ext_idx - 1] = ttl_s;
	}
}

static int32_t age_min(int64_t ts, int64_t rx, const struct ws_now *now)
{
	if (rx == WS_RX_NEVER) {
		return WS_AGE_NEVER;
	}
	int64_t s;

	if (ts > 0 && now->unix_s > 0) {
		s = now->unix_s - ts;
	} else {
		s = now->mono - rx;
	}
	if (s < 0) {
		s = 0;
	}
	int64_t m = s / 60;

	return m > WS_AGE_NEVER ? WS_AGE_NEVER : (int32_t)m;
}

int32_t ws_vars_fc_age(const struct ws_vars *vars, const struct ws_now *now)
{
	return age_min(vars->fc_ts, vars->fc_rx, now);
}

int32_t ws_vars_obs_age(const struct ws_vars *vars, const struct ws_now *now)
{
	return age_min(vars->obs_ts, vars->obs_rx, now);
}

bool ws_vars_fallback_active(const struct ws_vars *vars, const struct ws_now *now)
{
	return ws_vars_fc_age(vars, now) > WS_FALLBACK_FC_AGE_MIN &&
	       ws_vars_obs_age(vars, now) <= WS_FALLBACK_OBS_AGE_MIN;
}

static bool forecast_only(int id)
{
	switch (id) {
	case WS_V_FC_TMAX:
	case WS_V_FC_TMIN:
	case WS_V_FC_RAIN_FROM:
	case WS_V_FC_RAIN_TO:
	case WS_V_FC_RAIN_IN:
	case WS_V_FC_SNOW:
	case WS_V_FC_STORM:
	case WS_V_FC_NEXT_NAME:
	case WS_V_FC_NEXT_T:
	case WS_V_FC_NEXT_COND:
	case WS_V_FC_HOURS:
		return true;
	default:
		return false;
	}
}

static int fallback_of(int id)
{
	switch (id) {
	case WS_V_OUT_T:
		return WS_V_OBS_T;
	case WS_V_OUT_COND:
		return WS_V_OBS_COND;
	case WS_V_OUT_WIND:
		return WS_V_OBS_WIND;
	case WS_V_OUT_WIND_DIR:
		return WS_V_OBS_WIND_DIR;
	case WS_V_OUT_RH:
		return WS_V_OBS_RH;
	case WS_V_OUT_P:
		return WS_V_OBS_P;
	case WS_V_FC_ICE:
		return WS_V_OBS_ICE;
	default:
		return -1;
	}
}

static bool raw_get(const struct ws_vars *vars, int id, const struct ws_now *now,
		    struct ws_value *out)
{
	const struct ws_var_info *inf = &infos[id];
	const struct ws_var_slot *s = &vars->v[id];
	uint32_t ttl = inf->ttl_s;

	out->known = false;
	out->type = inf->type;
	out->src = inf->src;
	out->num = 0;
	out->str = NULL;
	out->hours = NULL;
	if (id >= WS_V_EXT1 && id <= WS_V_EXT8) {
		ttl = vars->ext_ttl[id - WS_V_EXT1];
	}
	if (!s->set) {
		return false;
	}
	if (ttl && now->mono - s->updated > (int64_t)ttl) {
		return false;
	}
	out->known = true;
	if (inf->type == WS_VT_HOURS) {
		out->hours = &vars->hours;
	} else if (s->is_str) {
		out->type = WS_VT_STR;
		out->str = s->str;
	} else {
		if (inf->type == WS_VT_ANY) {
			out->type = WS_VT_NUM;
		}
		out->num = s->num;
	}
	return true;
}

static bool rain_in(const struct ws_vars *vars, const struct ws_now *now, struct ws_value *out)
{
	struct ws_value from, to, hh, mm;

	out->known = false;
	out->type = WS_VT_NUM;
	out->src = WS_SRC_FORECAST;
	if (!raw_get(vars, WS_V_FC_RAIN_FROM, now, &from) || from.num < 0 ||
	    !raw_get(vars, WS_V_TIME_HOUR, now, &hh) || !raw_get(vars, WS_V_TIME_MIN, now, &mm)) {
		return false;
	}
	int f = from.num / 10;
	int t = raw_get(vars, WS_V_FC_RAIN_TO, now, &to) && to.num >= 0 ? to.num / 10 : f + 1;
	int cur = (hh.num / 10) * 60 + mm.num / 10;
	int start = f * 60, end = t * 60;
	bool inside;

	if (start <= end) {
		inside = cur >= start && cur < end;
	} else { /* through midnight */
		inside = cur >= start || cur < end;
	}
	int m = inside ? 0 : ((start - cur) % 1440 + 1440) % 1440;

	out->known = true;
	out->num = m * 10;
	return true;
}

bool ws_vars_get(const struct ws_vars *vars, int id, const struct ws_now *now, struct ws_value *out)
{
	if (id < 0 || id >= WS_V_COUNT) {
		out->known = false;
		return false;
	}
	bool fb = ws_vars_fallback_active(vars, now);

	if (fb && forecast_only(id)) {
		raw_get(vars, id, now, out);
		out->known = false;
		return false;
	}
	if (fb && fallback_of(id) >= 0) {
		bool k = raw_get(vars, fallback_of(id), now, out);

		out->type = infos[id].type;
		return k;
	}
	switch (id) {
	case WS_V_FC_AGE:
		*out = (struct ws_value){
			true, WS_VT_NUM, WS_SRC_FORECAST, ws_vars_fc_age(vars, now) * 10,
			NULL, NULL};
		return true;
	case WS_V_OBS_AGE:
		*out = (struct ws_value){
			true, WS_VT_NUM, WS_SRC_METAR, ws_vars_obs_age(vars, now) * 10, NULL, NULL};
		return true;
	case WS_V_FC_RAIN_IN:
		return rain_in(vars, now, out);
	default:
		return raw_get(vars, id, now, out);
	}
}

int ws_value_format(const struct ws_value *v, int var_type, char *buf, size_t len)
{
	if (!v->known) {
		return snprintf(buf, len, "--");
	}
	switch (v->type) {
	case WS_VT_STR:
		return snprintf(buf, len, "%s", v->str);
	case WS_VT_BOOL:
		return snprintf(buf, len, "%s", v->num ? "true" : "false");
	case WS_VT_COND:
		return snprintf(buf, len, "%s", ws_cond_name(v->num));
	case WS_VT_DIR:
		return snprintf(buf, len, "%s", ws_dir_name(v->num));
	case WS_VT_HOURS:
		return snprintf(buf, len, "%u h", v->hours ? v->hours->n : 0);
	default: {
		int32_t a = v->num < 0 ? -v->num : v->num;

		if (a % 10) {
			return snprintf(buf, len, "%s%ld.%ld", v->num < 0 ? "-" : "",
					(long)(a / 10), (long)(a % 10));
		}
		return snprintf(buf, len, "%ld", (long)(v->num / 10));
	}
	}
}

/* ---- forecast ---- */

#define FC_TOKS 160

static void fc_num(struct ws_vars *vars, const struct ws_json *j, int obj, const char *key, int id,
		   int64_t mono)
{
	int32_t d;

	if (ws_json_deci(j, ws_json_get(j, obj, key), &d)) {
		ws_vars_set_num(vars, id, d, mono);
	} else {
		ws_vars_clear(vars, id);
	}
}

static void fc_bool(struct ws_vars *vars, const struct ws_json *j, int obj, const char *key, int id,
		    int64_t mono)
{
	bool b;

	if (ws_json_bool(j, ws_json_get(j, obj, key), &b)) {
		ws_vars_set_num(vars, id, b, mono);
	} else {
		ws_vars_clear(vars, id);
	}
}

static void fc_enum(struct ws_vars *vars, const struct ws_json *j, int obj, const char *key, int id,
		    bool dir, int64_t mono)
{
	char s[16];
	int e = -1;

	if (ws_json_str(j, ws_json_get(j, obj, key), s, sizeof(s)) > 0) {
		e = dir ? ws_dir_parse(s) : ws_cond_parse(s);
	}
	if (e >= 0) {
		ws_vars_set_num(vars, id, e, mono);
	} else {
		ws_vars_clear(vars, id);
	}
}

int ws_forecast_apply(struct ws_vars *vars, const char *json, size_t len, int64_t mono)
{
	struct ws_jtok toks[FC_TOKS];
	struct ws_json j;

	if (ws_json_parse(&j, json, len, toks, FC_TOKS) < 0 || !ws_json_is(&j, 0, WS_J_OBJ)) {
		return -1;
	}
	int32_t ts;

	vars->fc_ts = ws_json_int(&j, ws_json_get(&j, 0, "ts"), &ts) ? ts : 0;
	vars->fc_rx = mono;

	int now = ws_json_get(&j, 0, "now");

	fc_num(vars, &j, now, "t", WS_V_OUT_T, mono);
	fc_enum(vars, &j, now, "cond", WS_V_OUT_COND, false, mono);
	fc_num(vars, &j, now, "wind", WS_V_OUT_WIND, mono);
	fc_enum(vars, &j, now, "dir", WS_V_OUT_WIND_DIR, true, mono);
	fc_num(vars, &j, now, "rh", WS_V_OUT_RH, mono);
	fc_num(vars, &j, now, "p", WS_V_OUT_P, mono);

	int day = ws_json_get(&j, 0, "day");

	fc_num(vars, &j, day, "max", WS_V_FC_TMAX, mono);
	fc_num(vars, &j, day, "min", WS_V_FC_TMIN, mono);

	/* "rain": {"from": 15, "to": 19}; null or missing "from" means no rain */
	int rain = ws_json_get(&j, 0, "rain");
	int32_t d;

	if (ws_json_is(&j, rain, WS_J_OBJ) && ws_json_deci(&j, ws_json_get(&j, rain, "from"), &d)) {
		ws_vars_set_num(vars, WS_V_FC_RAIN_FROM, d, mono);
		fc_num(vars, &j, rain, "to", WS_V_FC_RAIN_TO, mono);
	} else if (rain >= 0) {
		ws_vars_set_num(vars, WS_V_FC_RAIN_FROM, -10, mono);
		ws_vars_set_num(vars, WS_V_FC_RAIN_TO, -10, mono);
	} else {
		ws_vars_clear(vars, WS_V_FC_RAIN_FROM);
		ws_vars_clear(vars, WS_V_FC_RAIN_TO);
	}

	int flags = ws_json_get(&j, 0, "flags");

	fc_bool(vars, &j, flags, "snow", WS_V_FC_SNOW, mono);
	fc_bool(vars, &j, flags, "ice", WS_V_FC_ICE, mono);
	fc_bool(vars, &j, flags, "storm", WS_V_FC_STORM, mono);

	int next = ws_json_get(&j, 0, "next");
	char name[WS_VAR_STR_LEN];

	if (ws_json_str(&j, ws_json_get(&j, next, "name"), name, sizeof(name)) >= 0) {
		ws_vars_set_str(vars, WS_V_FC_NEXT_NAME, name, mono);
	} else {
		ws_vars_clear(vars, WS_V_FC_NEXT_NAME);
	}
	fc_num(vars, &j, next, "t", WS_V_FC_NEXT_T, mono);
	fc_enum(vars, &j, next, "cond", WS_V_FC_NEXT_COND, false, mono);

	/* optional "hours": [[t, pop], ...] for the graph element */
	int hours = ws_json_get(&j, 0, "hours");
	struct ws_hours h = {0};

	for (int k = ws_json_first(&j, hours);
	     k >= 0 && ws_json_is(&j, hours, WS_J_ARR) && h.n < WS_HOURS_MAX;
	     k = ws_json_next(&j, hours, k)) {
		int32_t t, pop;

		if (!ws_json_deci(&j, ws_json_at(&j, k, 0), &t) ||
		    !ws_json_int(&j, ws_json_at(&j, k, 1), &pop)) {
			break;
		}
		h.t[h.n] = (int16_t)((t >= 0 ? t + 5 : t - 5) / 10);
		h.pop[h.n] = (uint8_t)(pop < 0 ? 0 : (pop > 100 ? 100 : pop));
		h.n++;
	}
	if (h.n) {
		ws_vars_set_hours(vars, &h, mono);
	} else {
		ws_vars_clear(vars, WS_V_FC_HOURS);
	}
	touch(vars);
	return 0;
}

int ws_ext_apply(struct ws_vars *vars, int ext_idx, const char *field, const char *payload,
		 size_t len, int64_t mono)
{
	struct ws_jtok toks[64];
	struct ws_json j;
	int id = WS_V_EXT1 + ext_idx - 1;
	char buf[WS_VAR_STR_LEN];

	if (ext_idx < 1 || ext_idx > 8) {
		return -1;
	}
	if (ws_json_parse(&j, payload, len, toks, 64) < 0) {
		/* plain text payload, e.g. "21.5" or "ON" */
		if (field && field[0]) {
			return -1;
		}
		size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;

		memcpy(buf, payload, n);
		buf[n] = '\0';
		ws_vars_set_str(vars, id, buf, mono);
		return 0;
	}
	int tok = 0;

	if (field && field[0]) {
		const char *p = field;

		while (*p && tok >= 0) {
			char key[32];
			size_t n = strcspn(p, ".");

			if (n >= sizeof(key)) {
				return -1;
			}
			memcpy(key, p, n);
			key[n] = '\0';
			tok = ws_json_get(&j, tok, key);
			p += n;
			if (*p == '.') {
				p++;
			}
		}
		if (tok < 0) {
			return -1;
		}
	}
	int32_t d;
	bool b;

	if (ws_json_deci(&j, tok, &d)) {
		ws_vars_set_num(vars, id, d, mono);
	} else if (ws_json_bool(&j, tok, &b)) {
		ws_vars_set_num(vars, id, b ? 10 : 0, mono);
	} else if (ws_json_str(&j, tok, buf, sizeof(buf)) >= 0) {
		ws_vars_set_str(vars, id, buf, mono);
	} else {
		return -1;
	}
	return 0;
}

void ws_vars_set_obs_time(struct ws_vars *vars, int64_t obs_unix, int64_t mono)
{
	vars->obs_ts = obs_unix;
	vars->obs_rx = mono;
	touch(vars);
}
