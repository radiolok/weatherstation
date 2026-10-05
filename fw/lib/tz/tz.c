/* POSIX TZ, civil time and sun position, see ws/tz.h. */
#include <ctype.h>
#include <math.h>
#include <string.h>

#include <ws/tz.h>

/* ---- civil dates (H. Hinnant's algorithms) ---- */

int64_t ws_days_from_civil(int y, int m, int d)
{
	y -= m <= 2;
	int64_t era = (y >= 0 ? y : y - 399) / 400;
	int64_t yoe = y - era * 400;
	int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

	return era * 146097 + doe - 719468;
}

void ws_civil_from_days(int64_t z, int *y, int *m, int *d)
{
	z += 719468;
	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	int64_t doe = z - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t yy = yoe + era * 400;
	int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153;

	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*m = (int)(mp < 10 ? mp + 3 : mp - 9);
	*y = (int)(yy + (*m <= 2));
}

static bool leap(int y)
{
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

void ws_tm_from_unix(int64_t t, struct ws_tm *tm)
{
	int64_t days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
	int64_t sec = t - days * 86400;

	ws_civil_from_days(days, &tm->year, &tm->month, &tm->day);
	tm->hour = (int)(sec / 3600);
	tm->min = (int)(sec / 60 % 60);
	tm->sec = (int)(sec % 60);
	/* 1970-01-01 was a Thursday (4) */
	int64_t w = (days + 3) % 7;

	tm->dow = (int)(w < 0 ? w + 7 : w) + 1;
	tm->yday = (int)(days - ws_days_from_civil(tm->year, 1, 1));
}

/* ---- POSIX TZ ---- */

static const char *parse_name(const char *p, char *out, size_t n)
{
	size_t i = 0;

	if (*p == '<') {
		p++;
		while (*p && *p != '>') {
			if (i + 1 < n) {
				out[i++] = *p;
			}
			p++;
		}
		if (*p != '>') {
			return NULL;
		}
		p++;
	} else {
		while (isalpha((unsigned char)*p)) {
			if (i + 1 < n) {
				out[i++] = *p;
			}
			p++;
		}
	}
	out[i] = '\0';
	return i >= 3 ? p : NULL;
}

/* [+|-]hh[:mm[:ss]] -> seconds */
static const char *parse_hms(const char *p, int32_t *out, bool allow_sign)
{
	int sign = 1;
	int32_t v[3] = {0, 0, 0};

	if (allow_sign && (*p == '+' || *p == '-')) {
		sign = *p == '-' ? -1 : 1;
		p++;
	}
	for (int k = 0; k < 3; k++) {
		if (!isdigit((unsigned char)*p)) {
			if (k == 0) {
				return NULL;
			}
			break;
		}
		while (isdigit((unsigned char)*p)) {
			v[k] = v[k] * 10 + (*p - '0');
			p++;
		}
		if (*p != ':' || k == 2) {
			break;
		}
		p++;
	}
	if (v[0] > 167 || v[1] > 59 || v[2] > 59) {
		return NULL;
	}
	*out = sign * (v[0] * 3600 + v[1] * 60 + v[2]);
	return p;
}

static const char *parse_num(const char *p, int *out)
{
	if (!isdigit((unsigned char)*p)) {
		return NULL;
	}
	*out = 0;
	while (isdigit((unsigned char)*p)) {
		*out = *out * 10 + (*p - '0');
		p++;
	}
	return p;
}

static const char *parse_rule(const char *p, struct ws_tz_rule *r)
{
	int a, b, c;

	memset(r, 0, sizeof(*r));
	r->time = 7200;
	if (*p == 'M') {
		p = parse_num(p + 1, &a);
		if (!p || *p != '.' || !(p = parse_num(p + 1, &b)) || *p != '.' ||
		    !(p = parse_num(p + 1, &c))) {
			return NULL;
		}
		if (a < 1 || a > 12 || b < 1 || b > 5 || c > 6) {
			return NULL;
		}
		r->kind = 0;
		r->month = (uint8_t)a;
		r->week = (uint8_t)b;
		r->wday = (uint8_t)c;
	} else if (*p == 'J') {
		p = parse_num(p + 1, &a);
		if (!p || a < 1 || a > 365) {
			return NULL;
		}
		r->kind = 1;
		r->day = (uint16_t)a;
	} else {
		p = parse_num(p, &a);
		if (!p || a > 365) {
			return NULL;
		}
		r->kind = 2;
		r->day = (uint16_t)a;
	}
	if (*p == '/') {
		/* POSIX extension: the time may be negative or > 24 h */
		p = parse_hms(p + 1, &r->time, true);
		if (!p) {
			return NULL;
		}
	}
	return p;
}

int ws_tz_parse(const char *s, struct ws_tz *tz)
{
	const char *p = s;

	memset(tz, 0, sizeof(*tz));
	if (!p || !(p = parse_name(p, tz->std_name, sizeof(tz->std_name)))) {
		return -1;
	}
	int32_t off;

	if (!(p = parse_hms(p, &off, true))) {
		return -1;
	}
	tz->std_off = -off; /* POSIX: west positive */
	tz->dst_off = tz->std_off;
	if (!*p) {
		return 0;
	}
	if (!(p = parse_name(p, tz->dst_name, sizeof(tz->dst_name)))) {
		return -1;
	}
	tz->has_dst = true;
	tz->dst_off = tz->std_off + 3600;
	if (*p && *p != ',') {
		if (!(p = parse_hms(p, &off, true))) {
			return -1;
		}
		tz->dst_off = -off;
	}
	if (*p != ',') {
		/* no rules: US defaults are not assumed, DST name without rules */
		return -1;
	}
	if (!(p = parse_rule(p + 1, &tz->start)) || *p != ',' ||
	    !(p = parse_rule(p + 1, &tz->end)) || *p) {
		return -1;
	}
	return 0;
}

/* Local midnight (in days since epoch) of the rule date in `year`. */
static int64_t rule_day(const struct ws_tz_rule *r, int year)
{
	if (r->kind == 1) {
		int d = r->day - 1;

		if (leap(year) && r->day >= 60) {
			d++;
		}
		return ws_days_from_civil(year, 1, 1) + d;
	}
	if (r->kind == 2) {
		return ws_days_from_civil(year, 1, 1) + r->day;
	}
	int64_t first = ws_days_from_civil(year, r->month, 1);
	/* weekday of the first: 0 = Sunday */
	int64_t w = (first + 4) % 7;
	int wd0 = (int)(w < 0 ? w + 7 : w);
	int64_t d = first + ((r->wday - wd0 + 7) % 7) + (r->week - 1) * 7;
	int mdays[] = {31, leap(year) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	while (d >= first + mdays[r->month - 1]) {
		d -= 7; /* week 5 = last */
	}
	return d;
}

int32_t ws_tz_offset(const struct ws_tz *tz, int64_t utc, bool *is_dst)
{
	bool dst = false;

	if (tz->has_dst) {
		struct ws_tm tm;

		ws_tm_from_unix(utc + tz->std_off, &tm);
		/* transitions in UTC: start is given in standard time, end in DST */
		int64_t start =
			rule_day(&tz->start, tm.year) * 86400 + tz->start.time - tz->std_off;
		int64_t end = rule_day(&tz->end, tm.year) * 86400 + tz->end.time - tz->dst_off;

		if (start < end) {
			dst = utc >= start && utc < end; /* northern hemisphere */
		} else {
			dst = !(utc >= end && utc < start); /* southern hemisphere */
		}
	}
	if (is_dst) {
		*is_dst = dst;
	}
	return dst ? tz->dst_off : tz->std_off;
}

void ws_localtime(const struct ws_tz *tz, int64_t utc, struct ws_tm *tm)
{
	ws_tm_from_unix(utc + ws_tz_offset(tz, utc, NULL), tm);
}

/* ---- sun (NOAA solar calculator, accurate to about a minute) ---- */

#define PI  3.14159265358979323846
#define RAD (PI / 180.0)

static void solar(double jd, double *decl, double *eqtime)
{
	double t = (jd - 2451545.0) / 36525.0;
	double l0 = fmod(280.46646 + t * (36000.76983 + t * 0.0003032), 360.0);
	double m = 357.52911 + t * (35999.05029 - 0.0001537 * t);
	double e = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
	double c = sin(m * RAD) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
		   sin(2 * m * RAD) * (0.019993 - 0.000101 * t) + sin(3 * m * RAD) * 0.000289;
	double true_long = l0 + c;
	double omega = 125.04 - 1934.136 * t;
	double lambda = true_long - 0.00569 - 0.00478 * sin(omega * RAD);
	double eps0 = 23.0 +
		      (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
	double eps = eps0 + 0.00256 * cos(omega * RAD);
	double y = tan(eps * RAD / 2) * tan(eps * RAD / 2);

	*decl = asin(sin(eps * RAD) * sin(lambda * RAD));
	*eqtime = 4.0 / RAD *
		  (y * sin(2 * l0 * RAD) - 2 * e * sin(m * RAD) +
		   4 * e * y * sin(m * RAD) * cos(2 * l0 * RAD) - 0.5 * y * y * sin(4 * l0 * RAD) -
		   1.25 * e * e * sin(2 * m * RAD));
}

int ws_sun_times(double lat, double lon, int64_t utc, int64_t *rise, int64_t *set)
{
	int64_t day = utc >= 0 ? utc / 86400 : -((-utc + 86399) / 86400);
	double noon_min = 720.0 - 4.0 * lon;
	double decl, eqt, cos_ha;

	/* iterate once with the time of the solar noon */
	double jd = 2440587.5 + (double)day + noon_min / 1440.0;

	solar(jd, &decl, &eqt);
	noon_min = 720.0 - 4.0 * lon - eqt;
	jd = 2440587.5 + (double)day + noon_min / 1440.0;
	solar(jd, &decl, &eqt);
	cos_ha = (cos(90.833 * RAD) - sin(lat * RAD) * sin(decl)) / (cos(lat * RAD) * cos(decl));
	if (cos_ha < -1.0) {
		return 1;
	}
	if (cos_ha > 1.0) {
		return -1;
	}
	double ha = acos(cos_ha) / RAD;

	noon_min = 720.0 - 4.0 * lon - eqt;
	*rise = day * 86400 + (int64_t)floor((noon_min - 4.0 * ha) * 60.0 + 0.5);
	*set = day * 86400 + (int64_t)floor((noon_min + 4.0 * ha) * 60.0 + 0.5);
	return 0;
}

bool ws_sun_up(double lat, double lon, int64_t utc)
{
	/* the solar day around local noon: use the UTC day of the local noon */
	int64_t shifted = utc + (int64_t)(lon / 15.0 * 3600.0);
	int64_t day_utc = (shifted >= 0 ? shifted / 86400 : -((-shifted + 86399) / 86400)) * 86400 +
			  43200 - (int64_t)(lon / 15.0 * 3600.0);
	int64_t rise, set;
	int r = ws_sun_times(lat, lon, day_utc, &rise, &set);

	if (r != 0) {
		return r > 0;
	}
	return utc >= rise && utc < set;
}
