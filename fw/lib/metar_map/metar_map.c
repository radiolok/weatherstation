/* METAR report -> obs.* variables, see ws/metar_map.h. */
#include <math.h>
#include <string.h>

#include <ws/metar_map.h>
#include <ws/sign.h>
#include <ws/tz.h>

#define PRECIP_RAIN (WS_WX_RA | WS_WX_DZ | WS_WX_GR | WS_WX_GS | WS_WX_UP)
#define PRECIP_SNOW (WS_WX_SN | WS_WX_SG | WS_WX_PL | WS_WX_IC)

/* half away from zero, like the rest of the deci arithmetic */
static int32_t round_deci(double v)
{
	return (int32_t)(v < 0 ? -floor(-v * 10.0 + 0.5) : floor(v * 10.0 + 0.5));
}

static int32_t wind_deci_mps(int speed, int units)
{
	switch (units) {
	case WS_WIND_MPS:
		return speed * 10;
	case WS_WIND_KMH:
		return round_deci(speed / 3.6);
	default:
		return round_deci(speed * 0.514444);
	}
}

/* Relative humidity from t and dew point (Magnus, over water) */
static int32_t rh_deci(int t, int dew)
{
	const double a = 17.625, b = 243.04;
	double rh = 100.0 * exp(a * dew / (b + dew)) / exp(a * t / (b + t));

	if (rh > 100.0) {
		rh = 100.0;
	}
	return round_deci(rh);
}

static uint8_t cond_of(const struct ws_metar_obs *o, bool day)
{
	uint16_t wx = 0;
	bool thunder = false;

	for (int i = 0; i < o->n_ph && i < WS_METAR_MAX_PH; i++) {
		const struct ws_metar_ph *p = &o->ph[i];

		if (p->vicinity) {
			continue; /* VCSH, VCTS: not at the station */
		}
		wx |= p->codes;
		thunder |= p->thunder;
	}
	if (thunder || (wx & WS_WX_SQ)) {
		return WS_COND_STORM;
	}
	if (wx & PRECIP_SNOW) {
		/* RASN: snow when it is not warm */
		if (!(wx & PRECIP_RAIN) || !o->has_t || o->t <= 1) {
			return WS_COND_SNOW;
		}
		return WS_COND_RAIN;
	}
	if (wx & PRECIP_RAIN) {
		return WS_COND_RAIN;
	}
	if ((wx & WS_WX_FG) || o->cover >= WS_CLD_BKN) {
		return WS_COND_CLOUDY;
	}
	if (o->cover >= WS_CLD_FEW) {
		return day ? WS_COND_PCLOUD : WS_COND_PCLOUD_N;
	}
	return day ? WS_COND_CLEAR : WS_COND_NIGHT;
}

static bool ice_of(const struct ws_metar_obs *o)
{
	for (int i = 0; i < o->n_ph && i < WS_METAR_MAX_PH; i++) {
		if (o->ph[i].freezing && !o->ph[i].vicinity) {
			return true; /* FZRA, FZDZ, FZFG */
		}
	}
	for (int i = 0; i < o->n_re && i < WS_METAR_MAX_PH; i++) {
		if (o->re[i].freezing) {
			return true; /* REFZRA */
		}
		if ((o->re[i].codes & (WS_WX_RA | WS_WX_DZ)) && o->has_t && o->t <= 0) {
			return true; /* RERA, REDZ, RESHRA at t <= 0 */
		}
	}
	for (int i = 0; i < o->n_rwy && i < WS_METAR_MAX_RWY; i++) {
		uint8_t d = o->rwy_deposit[i];

		if (d == 3 || d == 7 || d == 9) {
			return true;
		}
	}
	return false;
}

int64_t ws_metar_obs_unix(int day, int hour, int minute, int64_t now_unix)
{
	if (day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
		return 0;
	}
	int y, m, d;

	ws_civil_from_days(now_unix / 86400, &y, &m, &d);
	/* this month, else the previous months (day 31 of a short month) */
	for (int k = 0; k < 3; k++) {
		int yy = y, mm = m - k;

		while (mm < 1) {
			mm += 12;
			yy--;
		}
		int64_t days = ws_days_from_civil(yy, mm, day);
		int cy, cm, cd;

		ws_civil_from_days(days, &cy, &cm, &cd);
		if (cm != mm) {
			continue; /* no such day in that month */
		}
		int64_t t = days * 86400 + hour * 3600 + minute * 60;

		if (t <= now_unix + 86400) {
			return t;
		}
	}
	return 0;
}

void ws_metar_map(const struct ws_metar_obs *o, int sun_up, int64_t now_unix,
		  struct ws_metar_out *out)
{
	memset(out, 0, sizeof(*out));
	if (o->has_t) {
		out->has_t = true;
		out->t = o->t * 10;
	}
	if (o->has_wind) {
		out->has_wind = true;
		out->wind = wind_deci_mps(o->wind_speed, o->wind_units);
		if (o->wind_dir >= 0 && o->wind_speed > 0) {
			out->has_dir = true;
			out->dir = (uint8_t)(((o->wind_dir % 360) + 22) / 45 % WS_DIR_COUNT);
		}
	}
	if (o->has_qfe_mm) {
		out->has_p = true;
		out->p = o->qfe_mm * 10;
	} else if (o->has_qfe_hpa) {
		out->has_p = true;
		out->p = round_deci(o->qfe_hpa * 0.750062);
	} else if (o->has_qnh) {
		out->has_p = true;
		out->p = round_deci(o->qnh_hpa * 0.750062);
	} else if (o->has_alt_a) {
		out->has_p = true;
		out->p = round_deci(o->alt_a * 0.254);
	}
	if (o->has_t && o->has_dew) {
		out->has_rh = true;
		out->rh = rh_deci(o->t, o->dew);
	}
	out->has_cond = true;
	out->cond = cond_of(o, sun_up != 0);
	out->ice = ice_of(o);
	out->obs_unix = ws_metar_obs_unix(o->day, o->hour, o->minute, now_unix);
}

static void set_or_clear(struct ws_vars *v, int id, bool has, int32_t num, int64_t mono)
{
	if (has) {
		ws_vars_set_num(v, id, num, mono);
	} else {
		ws_vars_clear(v, id);
	}
}

void ws_metar_apply(struct ws_vars *v, const struct ws_metar_out *o, int64_t mono)
{
	set_or_clear(v, WS_V_OBS_T, o->has_t, o->t, mono);
	set_or_clear(v, WS_V_OBS_COND, o->has_cond, o->cond, mono);
	set_or_clear(v, WS_V_OBS_WIND, o->has_wind, o->wind, mono);
	set_or_clear(v, WS_V_OBS_WIND_DIR, o->has_dir, o->dir, mono);
	set_or_clear(v, WS_V_OBS_P, o->has_p, o->p, mono);
	set_or_clear(v, WS_V_OBS_RH, o->has_rh, o->rh, mono);
	ws_vars_set_num(v, WS_V_OBS_ICE, o->ice ? 1 : 0, mono);
	ws_vars_set_obs_time(v, o->obs_unix, mono);
}

static bool word_at(const char *p, const char *end, const char *w)
{
	size_t n = strlen(w);

	return (size_t)(end - p) >= n && !memcmp(p, w, n) &&
	       (p + n == end || p[n] == ' ' || p[n] == '\r' || p[n] == '\n');
}

int ws_metar_extract(const char *body, size_t len, const char *icao, char *out, size_t cap)
{
	const char *p = body, *end = body + len;

	while (p < end) {
		const char *eol = memchr(p, '\n', (size_t)(end - p));
		const char *le = eol ? eol : end;
		const char *s = p;

		while (s < le && (*s == ' ' || *s == '\t')) {
			s++;
		}
		/* optional report type */
		if (word_at(s, le, "METAR") || word_at(s, le, "SPECI")) {
			s += 6;
		}
		if (word_at(s, le, icao)) {
			const char *e = le;

			while (e > s && (e[-1] == '\r' || e[-1] == ' ' || e[-1] == '=')) {
				e--;
			}
			size_t n = (size_t)(e - s);

			if (n + 1 > cap) {
				return -1;
			}
			memcpy(out, s, n);
			out[n] = '\0';
			return (int)n;
		}
		p = eol ? eol + 1 : end;
	}
	return -1;
}
