/*
 * One day of the factory set with a 10 s step on recorded forecasts: the
 * sequence of screens must match, and the sign must stay quiet.
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "common.h"

enum scenario {
	DRY,
	RAIN_AT_15,
	ICE,
	SERVER_GONE,
	SERVER_GONE_METAR,
};

struct change {
	int minute; /* minute of the day when the screen became current */
	const char *id;
};

struct run_result {
	struct change ch[16];
	int n;
	long flips;         /* flipped dots in 24 h */
	int max_quiet_cols; /* max columns flipped in one step without a screen change */
	bool skid_seen;
};

static const char *forecast_json(enum scenario sc, char *b, size_t n)
{
	const char *rain = sc == RAIN_AT_15 ? "{\"from\":15,\"to\":19}" : "null";

	snprintf(b, n,
		 "{\"now\":{\"t\":-2,\"cond\":\"pcloud\",\"wind\":5,\"dir\":\"nw\",\"rh\":78,"
		 "\"p\":748},\"day\":{\"max\":4,\"min\":-7},\"rain\":%s,\"flags\":{\"snow\":false,"
		 "\"ice\":%s,\"storm\":false},\"next\":{\"name\":\"night\",\"t\":-7,\"cond\":"
		 "\"night\"}}",
		 rain, sc == ICE ? "true" : "false");
	return b;
}

static bool frame_has(const struct ws_frame *f, const struct ws_bitmap *bm, int x0, int y0)
{
	for (int y = 0; y < bm->h; y++) {
		for (int x = 0; x < bm->w; x++) {
			bool on = bm->rows[y] & (1u << (bm->w - 1 - x));

			if (ws_frame_get(f, x0 + x, y0 + y) != on) {
				return false;
			}
		}
	}
	return true;
}

static void day_run(enum scenario sc, struct run_result *res)
{
	static struct ws_engine e;
	static struct ws_frame prev, cur;
	char fc[512];
	const int64_t t0 = 100000; /* monotonic seconds at 00:00 */

	memset(res, 0, sizeof(*res));
	zassert_ok(compile_factory());
	ws_vars_init(&vars);
	ws_engine_init(&e, &cfg, t0);
	ws_frame_clear(&prev);
	bool fb_prev = false;
	/* forecast published at 23:30 the day before */
	ws_forecast_apply(&vars, forecast_json(sc, fc, sizeof(fc)), strlen(fc), t0 - 1800);

	for (int s = 0; s < 86400; s += 10) {
		int64_t t = t0 + s;
		int minute = s / 60;

		set_clock(t, minute / 60, minute % 60, 3);
		ws_vars_set_num(&vars, WS_V_IN_T, 221, t);
		ws_vars_set_num(&vars, WS_V_IN_RH, 410, t);
		ws_vars_set_num(&vars, WS_V_IN_CO2, 6400, t);
		ws_vars_set_num(&vars, WS_V_IN_P, 7470, t);
		ws_vars_set_num(&vars, WS_V_IN_P_TREND, 0, t);
		bool server_up = !((sc == SERVER_GONE || sc == SERVER_GONE_METAR) && s > 10 * 3600);

		if (s % 1800 == 0 && server_up) {
			ws_forecast_apply(&vars, forecast_json(sc, fc, sizeof(fc)), strlen(fc), t);
		}
		if (sc == SERVER_GONE_METAR && s % 1800 == 0) {
			ws_vars_set_num(&vars, WS_V_OBS_T, 10, t);
			ws_vars_set_num(&vars, WS_V_OBS_COND, WS_COND_CLOUDY, t);
			ws_vars_set_num(&vars, WS_V_OBS_WIND, 30, t);
			ws_vars_set_num(&vars, WS_V_OBS_WIND_DIR, WS_DIR_W, t);
			ws_vars_set_num(&vars, WS_V_OBS_P, 7460, t);
			ws_vars_set_num(&vars, WS_V_OBS_RH, 800, t);
			ws_vars_set_num(&vars, WS_V_OBS_ICE, 0, t);
			ws_vars_set_obs_time(&vars, 0, t);
		}
		struct ws_now now = {t, 0};
		bool changed = ws_engine_step(&e, &vars, &now);
		/* switching to METAR is a data change, not zone rotation */
		bool fb = ws_vars_fallback_active(&vars, &now);
		bool data_changed = fb != fb_prev || s % 1800 == 0;

		fb_prev = fb;

		if (changed || res->n == 0) {
			zassert_true(res->n < 16);
			res->ch[res->n++] = (struct change){minute, cfg.screens[e.current].id};
		}
		struct ws_render_ctx rc = {&cfg, &vars, now, s};

		ws_render_screen(&rc, e.current, &cur);
		struct ws_frame_diff d = ws_frame_diff(&prev, &cur);

		res->flips += d.dots;
		if (!changed && !data_changed && d.cols > res->max_quiet_cols) {
			res->max_quiet_cols = d.cols;
		}
		if (e.current == cfg.def &&
		    frame_has(&cur, ws_picto_builtin("skid", true), 33, 0)) {
			res->skid_seen = true;
		}
		prev = cur;
	}
}

static void expect(const struct run_result *r, const struct change *want, int n)
{
	for (int i = 0; i < r->n; i++) {
		TC_PRINT("  %02d:%02d %s\n", r->ch[i].minute / 60, r->ch[i].minute % 60,
			 r->ch[i].id);
	}
	TC_PRINT("  flipped dots: %ld, max quiet columns per step: %d\n", r->flips,
		 r->max_quiet_cols);
	zassert_equal(r->n, n, "sequence length");
	for (int i = 0; i < n; i++) {
		zassert_equal(r->ch[i].minute, want[i].minute, "step %d", i);
		zassert_str_equal(r->ch[i].id, want[i].id, "step %d", i);
	}
	/* rotator zones change at most their 30 columns at a time */
	zassert_true(r->max_quiet_cols <= 30, "%d columns", r->max_quiet_cols);
	/* budget: two zones of 30 columns once a minute, ~ 1/3 of their dots */
	zassert_true(r->flips < 250000, "%ld dots", r->flips);
}

static struct run_result res;

ZTEST(screens, test_day_dry)
{
	const struct change want[] = {{0, "evening"}, {7 * 60, "main"}, {22 * 60, "evening"}};

	day_run(DRY, &res);
	expect(&res, want, 3);
	zassert_false(res.skid_seen);
}

ZTEST(screens, test_day_rain_at_15)
{
	const struct change want[] = {
		{0, "evening"},         {7 * 60, "main"},     {12 * 60, "rain"},
		{19 * 60 + 10, "main"}, {22 * 60, "evening"},
	};

	day_run(RAIN_AT_15, &res);
	expect(&res, want, 5);
}

ZTEST(screens, test_day_ice)
{
	const struct change want[] = {{0, "evening"}, {7 * 60, "main"}, {22 * 60, "evening"}};

	day_run(ICE, &res);
	expect(&res, want, 3);
	zassert_true(res.skid_seen, "no skid pictogram on the main screen");
}

ZTEST(screens, test_day_server_gone)
{
	const struct change want[] = {
		{0, "evening"},
		{7 * 60, "main"},
		{12 * 60 + 1, "noforecast"},
		{22 * 60, "evening"},
	};

	day_run(SERVER_GONE, &res);
	expect(&res, want, 4);
}

ZTEST(screens, test_day_server_gone_metar)
{
	const struct change want[] = {{0, "evening"}, {7 * 60, "main"}, {22 * 60, "evening"}};

	day_run(SERVER_GONE_METAR, &res);
	expect(&res, want, 3);
}
