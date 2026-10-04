/* POSIX TZ, civil time and sunrise/sunset against reference tables. */
#include <stdlib.h>
#include <zephyr/ztest.h>

#include <ws/tz.h>

static int64_t utc(int y, int m, int d, int h, int mi)
{
	return ws_days_from_civil(y, m, d) * 86400 + h * 3600 + mi * 60;
}

ZTEST(tz, test_civil)
{
	struct ws_tm tm;

	ws_tm_from_unix(0, &tm);
	zassert_equal(tm.year, 1970);
	zassert_equal(tm.dow, 4); /* Thursday */
	ws_tm_from_unix(utc(2026, 10, 4, 13, 45), &tm);
	zassert_equal(tm.day, 4);
	zassert_equal(tm.hour, 13);
	zassert_equal(tm.dow, 7); /* Sunday */
	ws_tm_from_unix(utc(2024, 2, 29, 0, 0), &tm);
	zassert_equal(tm.month, 2);
	zassert_equal(tm.day, 29);
	zassert_equal(tm.yday, 59);
}

ZTEST(tz, test_msk)
{
	struct ws_tz tz;
	struct ws_tm tm;
	bool dst;

	zassert_ok(ws_tz_parse("MSK-3", &tz));
	zassert_equal(tz.std_off, 3 * 3600);
	zassert_false(tz.has_dst);
	ws_localtime(&tz, utc(2026, 1, 1, 21, 30), &tm);
	zassert_equal(tm.day, 2);
	zassert_equal(tm.hour, 0);
	zassert_equal(ws_tz_offset(&tz, utc(2026, 7, 1, 0, 0), &dst), 10800);
	zassert_false(dst);
}

ZTEST(tz, test_cet_cest)
{
	struct ws_tz tz;
	bool dst;

	zassert_ok(ws_tz_parse("CET-1CEST,M3.5.0,M10.5.0/3", &tz));
	zassert_equal(tz.std_off, 3600);
	zassert_equal(tz.dst_off, 7200);
	/* 2026: DST from 29 March 01:00 UTC to 25 October 01:00 UTC */
	zassert_equal(ws_tz_offset(&tz, utc(2026, 3, 29, 0, 59), &dst), 3600);
	zassert_false(dst);
	zassert_equal(ws_tz_offset(&tz, utc(2026, 3, 29, 1, 0), &dst), 7200);
	zassert_true(dst);
	zassert_equal(ws_tz_offset(&tz, utc(2026, 10, 25, 0, 59), &dst), 7200);
	zassert_equal(ws_tz_offset(&tz, utc(2026, 10, 25, 1, 0), &dst), 3600);
}

ZTEST(tz, test_us_and_southern)
{
	struct ws_tz tz;
	bool dst;

	zassert_ok(ws_tz_parse("EST5EDT,M3.2.0,M11.1.0", &tz));
	/* 2026: 8 March 07:00 UTC .. 1 November 06:00 UTC */
	zassert_equal(ws_tz_offset(&tz, utc(2026, 3, 8, 6, 59), &dst), -5 * 3600);
	zassert_equal(ws_tz_offset(&tz, utc(2026, 3, 8, 7, 0), &dst), -4 * 3600);
	zassert_equal(ws_tz_offset(&tz, utc(2026, 11, 1, 5, 59), &dst), -4 * 3600);
	zassert_equal(ws_tz_offset(&tz, utc(2026, 11, 1, 6, 0), &dst), -5 * 3600);
	/* New Zealand: DST over the new year */
	zassert_ok(ws_tz_parse("NZST-12NZDT,M9.5.0,M4.1.0/3", &tz));
	zassert_equal(ws_tz_offset(&tz, utc(2026, 1, 15, 0, 0), &dst), 13 * 3600);
	zassert_true(dst);
	zassert_equal(ws_tz_offset(&tz, utc(2026, 6, 15, 0, 0), &dst), 12 * 3600);
	/* angle-bracket names and minutes */
	zassert_ok(ws_tz_parse("<+0545>-5:45", &tz));
	zassert_equal(tz.std_off, 5 * 3600 + 45 * 60);
	zassert_ok(ws_tz_parse("<-03>3", &tz));
	zassert_equal(tz.std_off, -3 * 3600);
}

ZTEST(tz, test_bad_strings)
{
	struct ws_tz tz;
	const char *bad[] = {"",
			     "M",
			     "MSK",
			     "MSK-",
			     "CET-1CEST",
			     "CET-1CEST,M3.5.0",
			     "CET-1CEST,M13.5.0,M10.5.0",
			     "X1"};

	for (unsigned int i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		zassert_equal(ws_tz_parse(bad[i], &tz), -1, "%s", bad[i]);
	}
}

/* Reference: timeanddate.com, Moscow (55.7558 N, 37.6173 E), times in UTC */
static void check_sun(double lat, double lon, int y, int m, int d, int rh, int rm, int sh, int sm)
{
	int64_t rise, set;

	zassert_ok(ws_sun_times(lat, lon, utc(y, m, d, 12, 0), &rise, &set));
	zassert_within(rise, utc(y, m, d, rh, rm), 180, "rise %d-%d-%d off by %d s", y, m, d,
		       (int)(rise - utc(y, m, d, rh, rm)));
	zassert_within(set, utc(y, m, d, sh, sm), 180, "set %d-%d-%d off by %d s", y, m, d,
		       (int)(set - utc(y, m, d, sh, sm)));
}

ZTEST(tz, test_sun_tables)
{
	/* Moscow: 21 Jun 2024 sunrise 03:44 MSK, sunset 21:18 MSK */
	check_sun(55.7558, 37.6173, 2024, 6, 21, 0, 44, 18, 18);
	/* Moscow: 21 Dec 2024 sunrise 08:58 MSK, sunset 15:57 MSK */
	check_sun(55.7558, 37.6173, 2024, 12, 21, 5, 58, 12, 57);
	/* London: 21 Jun 2024 sunrise 04:43 BST, sunset 21:21 BST */
	check_sun(51.5074, -0.1278, 2024, 6, 21, 3, 43, 20, 21);
	/* Murmansk: polar day in June, polar night in December */
	int64_t r, s;

	zassert_equal(ws_sun_times(68.97, 33.08, utc(2024, 6, 21, 12, 0), &r, &s), 1);
	zassert_equal(ws_sun_times(68.97, 33.08, utc(2024, 12, 21, 12, 0), &r, &s), -1);
	zassert_true(ws_sun_up(68.97, 33.08, utc(2024, 6, 21, 23, 0)));
	zassert_false(ws_sun_up(68.97, 33.08, utc(2024, 12, 21, 9, 0)));
	/* Moscow at local noon and midnight */
	zassert_true(ws_sun_up(55.7558, 37.6173, utc(2024, 12, 21, 9, 0)));
	zassert_false(ws_sun_up(55.7558, 37.6173, utc(2024, 12, 21, 21, 0)));
}

ZTEST_SUITE(tz, NULL, NULL, NULL, NULL, NULL);
