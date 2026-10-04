/*
 * METAR fallback (docs/screen-constructor.md, section 14): a decoded report
 * mapped to the obs.* variables.
 *
 * The decoder (au/metar_cpp, C++) fills struct ws_metar_obs through
 * ws_metar_decode() in metar_adapter.cpp; everything after that is plain C
 * and covered by ztest on real reports:
 *
 *   obs.t      temperature
 *   obs.wind   speed in m/s (KT and KMH converted), obs.wind_dir 8 sectors
 *              ("from", like the forecast), unknown for VRB and calm
 *   obs.p      QFE in mmHg from the remarks, else QFE hPa, else QNH / A
 *              converted to mmHg
 *   obs.rh     from temperature and dew point (Magnus)
 *   obs.cond   thunderstorm > snow > rain > fog/clouds > clear, the night
 *              variants when sun.up is false
 *   obs.ice    freezing phenomena (FZRA, FZDZ, FZFG); recent rain or
 *              drizzle (RERA, REDZ, REFZRA) at t <= 0; an icy runway
 *              (deposit 3 rime/frost, 7 ice, 9 frozen ruts)
 */
#ifndef WS_METAR_MAP_H_
#define WS_METAR_MAP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ws/vars.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS_METAR_MAX_PH  8
#define WS_METAR_MAX_RWY 8
#define WS_METAR_MAX_LEN 512

/* Weather phenomena codes as a bit mask */
enum ws_wx {
	WS_WX_RA = 1u << 0,  /* rain */
	WS_WX_DZ = 1u << 1,  /* drizzle */
	WS_WX_SN = 1u << 2,  /* snow */
	WS_WX_SG = 1u << 3,  /* snow grains */
	WS_WX_PL = 1u << 4,  /* ice pellets (PE/PL) */
	WS_WX_GR = 1u << 5,  /* hail */
	WS_WX_GS = 1u << 6,  /* small hail */
	WS_WX_IC = 1u << 7,  /* ice crystals */
	WS_WX_UP = 1u << 8,  /* unknown precipitation */
	WS_WX_FG = 1u << 9,  /* fog */
	WS_WX_BR = 1u << 10, /* mist */
	WS_WX_HZ = 1u << 11, /* haze, smoke, dust, sand, ash */
	WS_WX_SQ = 1u << 12, /* squalls, funnel cloud */
	WS_WX_SS = 1u << 13, /* sand or dust storm */
};

struct ws_metar_ph {
	uint16_t codes;   /* WS_WX_* */
	int8_t intensity; /* -1 light, 0 moderate, 1 heavy */
	bool freezing;    /* FZ */
	bool thunder;     /* TS */
	bool shower;      /* SH */
	bool vicinity;    /* VC: not at the station */
};

enum ws_metar_cover {
	WS_CLD_NONE, /* SKC, CLR, NSC, NCD, CAVOK or no group */
	WS_CLD_FEW,
	WS_CLD_SCT,
	WS_CLD_BKN,
	WS_CLD_OVC,
	WS_CLD_VV, /* sky obscured */
};

enum ws_metar_wind_units {
	WS_WIND_KT,
	WS_WIND_MPS,
	WS_WIND_KMH,
};

/* Runway deposit "unknown" */
#define WS_RWY_NONE 0xff

struct ws_metar_obs {
	char icao[5];
	int8_t day, hour, minute; /* observation time UTC, -1 unknown */

	bool has_t, has_dew;
	int16_t t, dew; /* whole degrees */

	bool has_wind;
	int16_t wind_dir; /* degrees, -1 variable (VRB) */
	int16_t wind_speed;
	uint8_t wind_units; /* enum ws_metar_wind_units */

	bool has_qnh, has_alt_a, has_qfe_mm, has_qfe_hpa;
	int16_t qnh_hpa;
	int16_t alt_a; /* hundredths of inHg, A2992 -> 2992 */
	int16_t qfe_mm, qfe_hpa;

	bool cavok;
	uint8_t cover; /* enum ws_metar_cover, the highest of all layers */

	uint8_t n_ph, n_re, n_rwy;
	struct ws_metar_ph ph[WS_METAR_MAX_PH]; /* present weather */
	struct ws_metar_ph re[WS_METAR_MAX_PH]; /* recent weather (RE...) */
	uint8_t rwy_deposit[WS_METAR_MAX_RWY];  /* deposit code 0..9 or WS_RWY_NONE */
};

/* Values for the obs.* variables; has_* false means unknown */
struct ws_metar_out {
	bool has_t, has_cond, has_wind, has_dir, has_p, has_rh;
	int32_t t;    /* deci-degrees */
	int32_t wind; /* deci-m/s */
	int32_t p;    /* deci-mmHg */
	int32_t rh;   /* deci-% */
	uint8_t cond; /* enum ws_cond */
	uint8_t dir;  /* enum ws_dir */
	bool ice;
	int64_t obs_unix; /* 0 if the report has no time */
};

/* Decodes a raw report with metar_cpp (C++). Returns 0 or -1 if the text is
 * not a METAR/SPECI. Implemented in metar_adapter.cpp. */
int ws_metar_decode(const char *raw, struct ws_metar_obs *obs);

/* Maps a decoded report. `sun_up` < 0 when unknown (taken as day).
 * `now_unix` resolves the day of month of the observation time. */
void ws_metar_map(const struct ws_metar_obs *obs, int sun_up, int64_t now_unix,
		  struct ws_metar_out *out);

/* Unix time of "DDHHMMZ" relative to now: the latest such moment that is not
 * more than a day ahead of now (a report from the previous month works).
 * Returns 0 for invalid fields. */
int64_t ws_metar_obs_unix(int day, int hour, int minute, int64_t now_unix);

/* Stores the result as obs.* (unknown values are cleared) and the
 * observation time for obs.age. */
void ws_metar_apply(struct ws_vars *vars, const struct ws_metar_out *out, int64_t mono);

/* Finds the report of `icao` in an HTTP body: plain text with one or more
 * reports, optionally prefixed with "METAR"/"SPECI" and preceded by a date
 * line (NOAA tgftp format). Copies one line without the trailing '='.
 * Returns its length or -1. */
int ws_metar_extract(const char *body, size_t len, const char *icao, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* WS_METAR_MAP_H_ */
