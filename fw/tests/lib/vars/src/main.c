/* Variable store, forecast parsing and the METAR fallback. */
#include <string.h>
#include <zephyr/ztest.h>

#include <ws/json.h>
#include <ws/sign.h>
#include <ws/vars.h>

static struct ws_vars v;

static void before(void *f)
{
	ws_vars_init(&v);
}

static int32_t num(int id, int64_t mono)
{
	struct ws_now now = {mono, 0};
	struct ws_value x;

	zassert_true(ws_vars_get(&v, id, &now, &x), "%s unknown", ws_var_name(id));
	return x.num;
}

static bool known(int id, int64_t mono)
{
	struct ws_now now = {mono, 0};
	struct ws_value x;

	return ws_vars_get(&v, id, &now, &x);
}

static const char *FC = "{\"ts\": 0, \"now\": {\"t\": -2, \"cond\": \"pcloud\", \"wind\": 5, "
			"\"dir\": \"nw\", \"rh\": 78, \"p\": 748}, \"day\": {\"max\": 4, "
			"\"min\": -7}, \"rain\": {\"from\": 15, \"to\": 19}, \"flags\": "
			"{\"snow\": false, \"ice\": true, \"storm\": false}, \"next\": {\"name\": "
			"\"\xD0\xBD\xD0\xBE\xD1\x87\xD1\x8C\", \"t\": -7, \"cond\": \"night\"}, "
			"\"src\": {\"now.t\": \"metar\"}}";

ZTEST(vars, test_lookup)
{
	zassert_equal(ws_var_lookup("out.t"), WS_V_OUT_T);
	zassert_equal(ws_var_lookup("fc.next"), WS_V_FC_NEXT_COND);
	zassert_equal(ws_var_lookup("ext.8"), WS_V_EXT8);
	zassert_equal(ws_var_lookup("nope"), -1);
	for (int i = 0; i < WS_V_COUNT; i++) {
		zassert_not_null(ws_var_info(i)->name, "%d", i);
		zassert_equal(ws_var_lookup(ws_var_name(i)), i);
	}
}

ZTEST(vars, test_forecast)
{
	zassert_ok(ws_forecast_apply(&v, FC, strlen(FC), 100));
	zassert_equal(num(WS_V_OUT_T, 100), -20);
	zassert_equal(num(WS_V_OUT_COND, 100), WS_COND_PCLOUD);
	zassert_equal(num(WS_V_OUT_WIND_DIR, 100), WS_DIR_NW);
	zassert_equal(num(WS_V_OUT_P, 100), 7480);
	zassert_equal(num(WS_V_FC_TMIN, 100), -70);
	zassert_equal(num(WS_V_FC_RAIN_FROM, 100), 150);
	zassert_equal(num(WS_V_FC_ICE, 100), 1);
	zassert_equal(num(WS_V_FC_NEXT_COND, 100), WS_COND_NIGHT);
	zassert_false(known(WS_V_FC_HOURS, 100));
	zassert_equal(num(WS_V_FC_AGE, 100 + 3600), 600);
	/* lifetime 3 h */
	zassert_false(known(WS_V_OUT_T, 100 + 3 * 3600 + 1));
	zassert_equal(ws_forecast_apply(&v, "{\"now\":", 7, 200), -1);
}

ZTEST(vars, test_rain_in)
{
	ws_forecast_apply(&v, FC, strlen(FC), 0);
	/* no clock yet: unknown */
	zassert_false(known(WS_V_FC_RAIN_IN, 0));
	ws_vars_set_num(&v, WS_V_TIME_HOUR, 120, 0);
	ws_vars_set_num(&v, WS_V_TIME_MIN, 300, 0);
	zassert_equal(num(WS_V_FC_RAIN_IN, 0), 1500); /* 12:30 -> 15:00 = 150 min */
	ws_vars_set_num(&v, WS_V_TIME_HOUR, 160, 0);
	zassert_equal(num(WS_V_FC_RAIN_IN, 0), 0); /* raining now */
	ws_vars_set_num(&v, WS_V_TIME_HOUR, 200, 0);
	zassert_equal(num(WS_V_FC_RAIN_IN, 0), (24 * 60 - 330) * 10); /* tomorrow 15:00 */

	const char *dry = "{\"now\": {\"t\": 5}, \"rain\": null}";

	ws_forecast_apply(&v, dry, strlen(dry), 0);
	zassert_equal(num(WS_V_FC_RAIN_FROM, 0), -10);
	zassert_false(known(WS_V_FC_RAIN_IN, 0));
	zassert_false(known(WS_V_OUT_COND, 0)); /* missing fields are cleared */
}

ZTEST(vars, test_metar_fallback)
{
	/* forecast at t=0, METAR at t=2 h */
	ws_forecast_apply(&v, FC, strlen(FC), 0);
	ws_vars_set_num(&v, WS_V_OBS_T, 35, 7200);
	ws_vars_set_num(&v, WS_V_OBS_ICE, 0, 7200);
	ws_vars_set_obs_time(&v, 0, 7200);

	struct ws_now now = {7200, 0};
	struct ws_value x;

	zassert_false(ws_vars_fallback_active(&v, &now)); /* forecast 120 min: not older */
	zassert_equal(num(WS_V_OUT_T, 7200), -20);
	now.mono = 7200 + 61;
	zassert_true(ws_vars_fallback_active(&v, &now));
	zassert_equal(num(WS_V_OUT_T, 7261), 35);
	ws_vars_get(&v, WS_V_OUT_T, &now, &x);
	zassert_equal(x.src, WS_SRC_METAR);
	zassert_equal(num(WS_V_FC_ICE, 7261), 0);
	zassert_false(known(WS_V_FC_TMAX, 7261));  /* forecast-only data is dropped */
	zassert_false(known(WS_V_OUT_COND, 7261)); /* no obs.cond yet */
	/* METAR older than 90 min: no fallback, and the forecast is past its 3 h
	 * lifetime, so out.t is unknown ("--") */
	now.mono = 7200 + 91 * 60;
	zassert_false(ws_vars_fallback_active(&v, &now));
	zassert_false(known(WS_V_OUT_T, now.mono));
}

ZTEST(vars, test_ages_never)
{
	zassert_equal(num(WS_V_FC_AGE, 0), WS_AGE_NEVER * 10);
	zassert_equal(num(WS_V_OBS_AGE, 0), WS_AGE_NEVER * 10);
	/* wall clock wins over reception time when both are known */
	ws_vars_set_obs_time(&v, 1000000, 50);
	struct ws_now now = {60, 1000000 + 600};
	struct ws_value x;

	ws_vars_get(&v, WS_V_OBS_AGE, &now, &x);
	zassert_equal(x.num, 100);
}

ZTEST(vars, test_ext)
{
	const char *p = "{\"temperature\": 21.46, \"nested\": {\"state\": \"ON\"}}";

	ws_vars_set_ext_ttl(&v, 1, 60);
	zassert_ok(ws_ext_apply(&v, 1, "temperature", p, strlen(p), 10));
	zassert_equal(num(WS_V_EXT1, 10), 215);
	zassert_false(known(WS_V_EXT1, 71));
	zassert_ok(ws_ext_apply(&v, 2, "nested.state", p, strlen(p), 10));
	struct ws_now now = {10, 0};
	struct ws_value x;

	zassert_true(ws_vars_get(&v, WS_V_EXT2, &now, &x));
	zassert_equal(x.type, WS_VT_STR);
	zassert_str_equal(x.str, "ON");
	zassert_ok(ws_ext_apply(&v, 3, "", "17.5", 4, 10));
	zassert_equal(num(WS_V_EXT3, 10), 175);
	zassert_ok(ws_ext_apply(&v, 4, "", "hello", 5, 10));
	zassert_not_equal(ws_ext_apply(&v, 4, "missing", p, strlen(p), 10), 0);
	zassert_not_equal(ws_ext_apply(&v, 9, "", "1", 1, 10), 0);
}

ZTEST(vars, test_change_seq)
{
	uint32_t s0 = v.seq;

	ws_vars_set_num(&v, WS_V_IN_CO2, 6000, 1);
	zassert_equal(v.seq, s0 + 1);
	ws_vars_set_num(&v, WS_V_IN_CO2, 6000, 2); /* same value: no change */
	zassert_equal(v.seq, s0 + 1);
	ws_vars_set_num(&v, WS_V_IN_CO2, 6010, 3);
	zassert_equal(v.seq, s0 + 2);
}

ZTEST(vars, test_format)
{
	struct ws_value x = {true, WS_VT_NUM, 0, -25, NULL, NULL};
	char b[16];

	ws_value_format(&x, WS_VT_NUM, b, sizeof(b));
	zassert_str_equal(b, "-2.5");
	x.num = 7480;
	ws_value_format(&x, WS_VT_NUM, b, sizeof(b));
	zassert_str_equal(b, "748");
	x.known = false;
	ws_value_format(&x, WS_VT_NUM, b, sizeof(b));
	zassert_str_equal(b, "--");
}

ZTEST(vars, test_json_out_and_in)
{
	static char buf[8192];
	static struct ws_jtok toks[256];
	struct ws_now now = {100, 0};
	struct ws_json j;
	const char *in = "{\"out.t\": -2.5, \"out.cond\": \"rain\", \"fc.ice\": true, "
			 "\"fc.hours\": [[1, 10], [3, 60]], \"fc.next_name\": \"ночь\", "
			 "\"nope\": 1, \"in.co2\": null, \"fc.age\": 150}";

	ws_vars_set_num(&v, WS_V_IN_CO2, 6000, 0);
	zassert_true(ws_json_parse(&j, in, strlen(in), toks, 256) > 0);
	zassert_equal(ws_vars_apply_json(&v, &j, 0, 100), 7);
	zassert_equal(num(WS_V_OUT_T, 100), -25);
	zassert_equal(num(WS_V_OUT_COND, 100), WS_COND_RAIN);
	zassert_false(known(WS_V_IN_CO2, 100));
	zassert_equal(num(WS_V_FC_AGE, 100), 1500);
	zassert_true(ws_vars_to_json(&v, &now, buf, sizeof(buf)) > 0);
	zassert_not_null(strstr(buf, "{\"name\":\"out.t\",\"value\":-2.5,\"unit\":\"°C\""));
	zassert_not_null(strstr(buf, "\"name\":\"fc.hours\",\"value\":[[1,10],[3,60]]"));
	zassert_not_null(strstr(buf, "\"name\":\"in.co2\",\"value\":null"));
	zassert_not_null(strstr(buf, "\"name\":\"fc.ice\",\"value\":true"));
}

ZTEST_SUITE(vars, NULL, NULL, before, NULL, NULL);
