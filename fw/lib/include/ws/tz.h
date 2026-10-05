/*
 * Local time without a time zone database: POSIX TZ strings
 * ("MSK-3", "CET-1CEST,M3.5.0,M10.5.0/3"), civil date conversion and the
 * sun position for sun.up.
 */
#ifndef WS_TZ_H_
#define WS_TZ_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ws_tz_rule {
	uint8_t kind; /* 0: Mm.w.d, 1: Jn (1..365, no Feb 29), 2: n (0..365) */
	uint8_t month, week, wday;
	uint16_t day;
	int32_t time; /* seconds after local midnight */
};

struct ws_tz {
	char std_name[8], dst_name[8];
	int32_t std_off; /* seconds east of UTC: local = utc + off */
	int32_t dst_off;
	bool has_dst;
	struct ws_tz_rule start, end;
};

struct ws_tm {
	int year, month, day; /* month 1..12 */
	int hour, min, sec;
	int dow;  /* 1 = Monday .. 7 = Sunday */
	int yday; /* 0..365 */
};

/* 0 or -1 for a malformed string. */
int ws_tz_parse(const char *s, struct ws_tz *tz);
/* Offset in seconds at the given UTC time. */
int32_t ws_tz_offset(const struct ws_tz *tz, int64_t utc, bool *is_dst);

int64_t ws_days_from_civil(int y, int m, int d);
void ws_civil_from_days(int64_t days, int *y, int *m, int *d);
void ws_tm_from_unix(int64_t t, struct ws_tm *tm);
/* Local broken-down time for a UTC instant. */
void ws_localtime(const struct ws_tz *tz, int64_t utc, struct ws_tm *tm);

/* Sunrise and sunset of the UTC day containing `utc` at lat/lon (degrees,
 * north and east positive), as unix times. Returns 0, or 1 for polar day
 * (sun always up), -1 for polar night. */
int ws_sun_times(double lat, double lon, int64_t utc, int64_t *rise, int64_t *set);
bool ws_sun_up(double lat, double lon, int64_t utc);

#ifdef __cplusplus
}
#endif

#endif
