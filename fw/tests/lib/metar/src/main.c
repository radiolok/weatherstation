/* METAR fallback: real reports through metar_cpp and the mapping to obs.* */
#include <string.h>
#include <zephyr/ztest.h>

#include <ws/metar_map.h>
#include <ws/sign.h>
#include <ws/tz.h>
#include <ws/vars.h>

static int64_t utc(int y, int m, int d, int h, int mi)
{
	return ws_days_from_civil(y, m, d) * 86400 + h * 3600 + mi * 60;
}

/* 2026-10-04 12:00 UTC */
#define NOW utc(2026, 10, 4, 12, 0)

static struct ws_metar_out map(const char *raw, int sun_up)
{
	struct ws_metar_obs o;
	struct ws_metar_out out;

	zassert_ok(ws_metar_decode(raw, &o), "%s", raw);
	ws_metar_map(&o, sun_up, NOW, &out);
	return out;
}

ZTEST(metar, test_freezing_rain)
{
	struct ws_metar_out o =
		map("UNNT 041130Z 24005MPS 9999 -FZRA OVC010 M02/M03 Q1012 R25/290245 NOSIG", 1);

	zassert_true(o.has_t);
	zassert_equal(o.t, -20);
	zassert_equal(o.cond, WS_COND_RAIN);
	zassert_true(o.ice, "FZRA");
	zassert_equal(o.wind, 50);
	zassert_true(o.has_dir);
	zassert_equal(o.dir, WS_DIR_SW); /* 240 degrees */
	zassert_equal(o.p, 7591);        /* 1012 hPa */
	zassert_equal(o.obs_unix, utc(2026, 10, 4, 11, 30));
	zassert_true(o.has_rh);
	zassert_within(o.rh, 930, 10);
}

ZTEST(metar, test_recent_rain_below_zero)
{
	/* RERA at -1: the wet surface froze; the runway is only wet */
	struct ws_metar_out o = map(
		"METAR UUEE 040900Z 18003MPS 9999 BKN015 M01/M02 Q1003 RERA R24L/290050 NOSIG=", 1);

	zassert_equal(o.cond, WS_COND_CLOUDY);
	zassert_true(o.ice);

	o = map("UUEE 040900Z 18003MPS 9999 BKN015 03/01 Q1003 RERA NOSIG", 1);
	zassert_false(o.ice, "RERA above zero is not ice");
	zassert_equal(o.cond, WS_COND_CLOUDY, "recent rain is not current rain");
}

ZTEST(metar, test_runway_all_icy_cavok_night_qfe)
{
	struct ws_metar_out o = map(
		"UNNT 040300Z 00000MPS CAVOK M15/M18 Q1030 R88/790250 NOSIG RMK QFE745/0994", 0);

	zassert_true(o.ice, "R88 deposit 7 (ice)");
	zassert_equal(o.cond, WS_COND_NIGHT);
	zassert_equal(o.p, 7450, "QFE from the remarks wins over QNH");
	zassert_true(o.has_wind);
	zassert_equal(o.wind, 0);
	zassert_false(o.has_dir, "calm has no direction");
	zassert_equal(o.t, -150);
}

ZTEST(metar, test_runway_rime_and_cleared)
{
	zassert_true(
		map("ULLI 040600Z 10002MPS 9999 SCT020 M04/M06 Q1021 R10L/390240 NOSIG", 1).ice);
	zassert_false(
		map("ULLI 040600Z 10002MPS 9999 SCT020 M04/M06 Q1021 R10L/CLRD62 NOSIG", 1).ice);
	zassert_false(
		map("ULLI 040600Z 10002MPS 9999 SCT020 M04/M06 Q1021 R10L/490240 NOSIG", 1).ice,
		"dry snow is not ice");
}

ZTEST(metar, test_us_units)
{
	struct ws_metar_out o = map("KJFK 041251Z 31012G20KT 10SM FEW250 18/06 A3002 RMK AO2", 1);

	zassert_equal(o.wind, 62); /* 12 kt */
	zassert_equal(o.dir, WS_DIR_NW);
	zassert_equal(o.p, 7625); /* 30.02 inHg */
	zassert_equal(o.cond, WS_COND_PCLOUD);
	zassert_within(o.rh, 450, 10);

	o = map("KJFK 041251Z 31012KT 10SM SCT250 18/06 A3002", 0);
	zassert_equal(o.cond, WS_COND_PCLOUD_N);
}

ZTEST(metar, test_kmh_and_variable)
{
	zassert_equal(map("LFPG 041200Z 18015KMH 9999 SCT030 15/08 Q1015", 1).wind, 42);
	struct ws_metar_out o = map("UUDD 041200Z VRB02MPS 9999 SCT030 15/08 Q1015", 1);

	zassert_false(o.has_dir);
	zassert_equal(o.wind, 20);
}

ZTEST(metar, test_conditions)
{
	zassert_equal(map("UUWW 041200Z 22008MPS 6000 +TSRA BKN020CB 21/17 Q1008", 1).cond,
		      WS_COND_STORM);
	zassert_equal(map("UWGG 041200Z 03004MPS 2000 -SN BKN008 M03/M04 Q1015", 1).cond,
		      WS_COND_SNOW);
	zassert_equal(map("UWGG 041200Z 03004MPS 2000 -RASN BKN008 03/01 Q1015", 1).cond,
		      WS_COND_RAIN, "wet snow above +1 shows as rain");
	zassert_equal(map("UWGG 041200Z 03004MPS 2000 -RASN BKN008 00/M01 Q1015", 1).cond,
		      WS_COND_SNOW);
	zassert_equal(map("UWGG 041200Z 03004MPS 9999 -SHRA SCT020CB 12/08 Q1015", 1).cond,
		      WS_COND_RAIN);
	zassert_equal(map("UWGG 041200Z 03004MPS 9999 VCSH SCT020 12/08 Q1015", 1).cond,
		      WS_COND_PCLOUD, "showers in the vicinity are not at the station");
	zassert_equal(map("UUDD 041200Z 20005MPS 9999 SCT030 15/08 Q1015 TEMPO 3000 -SHRA", 1).cond,
		      WS_COND_PCLOUD, "TEMPO is a forecast, not the observation");
	zassert_equal(map("UUDD 041200Z 20005MPS 9999 NSC 15/08 Q1015 NOSIG", 1).cond,
		      WS_COND_CLEAR);
	zassert_equal(map("UUDD 041200Z 20005MPS 9999 OVC030 15/08 Q1015 NOSIG", 1).cond,
		      WS_COND_CLOUDY);
}

ZTEST(metar, test_freezing_fog)
{
	struct ws_metar_out o = map("UNNT 040600Z 00000MPS 0150 FZFG VV001 M08/M09 Q1025", 1);

	zassert_equal(o.cond, WS_COND_CLOUDY);
	zassert_true(o.ice);
}

ZTEST(metar, test_not_a_report)
{
	struct ws_metar_obs o;

	zassert_equal(ws_metar_decode("HELLO WORLD", &o), -1);
	zassert_equal(ws_metar_decode("", &o), -1);
	zassert_equal(ws_metar_decode("No METAR found for UNNT", &o), -1);
}

ZTEST(metar, test_obs_time)
{
	zassert_equal(ws_metar_obs_unix(4, 11, 30, NOW), utc(2026, 10, 4, 11, 30));
	/* yesterday's late report */
	zassert_equal(ws_metar_obs_unix(3, 23, 30, NOW), utc(2026, 10, 3, 23, 30));
	/* last month */
	zassert_equal(ws_metar_obs_unix(30, 23, 30, utc(2026, 10, 1, 0, 10)),
		      utc(2026, 9, 30, 23, 30));
	/* new year */
	zassert_equal(ws_metar_obs_unix(31, 23, 50, utc(2027, 1, 1, 0, 5)),
		      utc(2026, 12, 31, 23, 50));
	/* a clock a bit behind the station */
	zassert_equal(ws_metar_obs_unix(4, 12, 30, NOW), utc(2026, 10, 4, 12, 30));
	zassert_equal(ws_metar_obs_unix(0, 12, 30, NOW), 0);
	zassert_equal(ws_metar_obs_unix(4, 24, 0, NOW), 0);
}

ZTEST(metar, test_apply_and_fallback)
{
	static struct ws_vars v;
	struct ws_now now = {.mono = 1000, .unix_s = NOW};
	struct ws_value val;
	struct ws_metar_out o = map("UNNT 041130Z 24005MPS 9999 -FZRA OVC010 M02/M03 Q1012", 1);

	ws_vars_init(&v);
	ws_metar_apply(&v, &o, now.mono);
	zassert_true(ws_vars_get(&v, WS_V_OBS_T, &now, &val));
	zassert_equal(val.num, -20);
	zassert_true(ws_vars_get(&v, WS_V_OBS_ICE, &now, &val));
	zassert_equal(val.num, 1);
	zassert_true(ws_vars_get(&v, WS_V_OBS_AGE, &now, &val));
	zassert_equal(val.num, 300); /* 30 min */
	/* no forecast ever: out.* come from METAR */
	zassert_true(ws_vars_fallback_active(&v, &now));
	zassert_true(ws_vars_get(&v, WS_V_OUT_T, &now, &val));
	zassert_equal(val.num, -20);
	zassert_equal(val.src, WS_SRC_METAR);
	zassert_true(ws_vars_get(&v, WS_V_FC_ICE, &now, &val));
	zassert_equal(val.num, 1);
	zassert_false(ws_vars_get(&v, WS_V_FC_TMAX, &now, &val),
		      "forecast-only values are unknown");

	/* a report without temperature clears obs.t */
	o.has_t = false;
	ws_metar_apply(&v, &o, now.mono);
	zassert_false(ws_vars_get(&v, WS_V_OBS_T, &now, &val));

	/* 91 minutes later the report is stale: no fallback */
	now.unix_s += 61 * 60;
	now.mono += 61 * 60;
	zassert_false(ws_vars_fallback_active(&v, &now));
}

ZTEST(metar, test_extract)
{
	char out[WS_METAR_MAX_LEN];
	const char *noaa =
		"2026/10/04 11:30\nUNNT 041130Z 24005MPS 9999 OVC010 M02/M03 Q1012 NOSIG\n";
	const char *awc = "METAR UUEE 041130Z 18003MPS CAVOK 05/01 Q1003 NOSIG=\r\n"
			  "METAR UNNT 041130Z 24005MPS 9999 OVC010 M02/M03 Q1012 NOSIG=\r\n";

	zassert_true(ws_metar_extract(noaa, strlen(noaa), "UNNT", out, sizeof(out)) > 0);
	zassert_str_equal(out, "UNNT 041130Z 24005MPS 9999 OVC010 M02/M03 Q1012 NOSIG");
	zassert_true(ws_metar_extract(awc, strlen(awc), "UNNT", out, sizeof(out)) > 0);
	zassert_str_equal(out, "UNNT 041130Z 24005MPS 9999 OVC010 M02/M03 Q1012 NOSIG");
	zassert_equal(ws_metar_extract(awc, strlen(awc), "UWGG", out, sizeof(out)), -1);
	zassert_equal(ws_metar_extract(awc, strlen(awc), "UNNT", out, 10), -1);
	/* "UNNTX" is another word */
	zassert_equal(ws_metar_extract("UNNTX 041130Z", 13, "UNNT", out, sizeof(out)), -1);
}

ZTEST_SUITE(metar, NULL, NULL, NULL, NULL, NULL);
