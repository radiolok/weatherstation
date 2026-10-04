/*
 * The constructor elements must draw the slot widgets of the sign simulator
 * (tools/sign-simulator/core.js) dot for dot.
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "common.h"
#include "sign_golden_gen.h"

#define N(a) (int)(sizeof(a) / sizeof((a)[0]))

static const char *widget_json(const char *key, int x, const char *align, char *b, size_t n)
{
	if (!strcmp(key, "range")) {
		snprintf(b, n,
			 "{\"type\":\"range\",\"form\":\"S2\",\"x\":%d,\"w\":30,\"align\":\"%s\"}",
			 x, align);
	} else if (!strcmp(key, "rain")) {
		snprintf(b, n,
			 "{\"type\":\"rain\",\"form\":\"S2\",\"x\":%d,\"w\":30,\"align\":\"%s\","
			 "\"hours\":16}",
			 x, align);
	} else if (!strcmp(key, "wind")) {
		snprintf(b, n,
			 "{\"type\":\"wind\",\"form\":\"S2\",\"x\":%d,\"w\":30,\"align\":\"%s\"}",
			 x, align);
	} else if (!strcmp(key, "press")) {
		snprintf(
			b, n,
			"{\"type\":\"pressure\",\"form\":\"S2\",\"x\":%d,\"w\":30,\"align\":\"%s\","
			"\"trend\":0}",
			x, align);
	} else if (!strcmp(key, "home")) {
		snprintf(b, n,
			 "{\"type\":\"number\",\"form\":\"S\",\"var\":\"in.t\",\"x\":%d,\"y\":0,"
			 "\"w\":30,\"align\":\"%s\",\"decimals\":1,\"suffix\":\"\xC2\xB0\","
			 "\"picto\":\"home\"},"
			 "{\"type\":\"humidity\",\"form\":\"S\",\"var\":\"in.rh\",\"x\":%d,\"y\":6,"
			 "\"w\":30,\"align\":\"%s\"}",
			 x, align, x, align);
	} else if (!strcmp(key, "co2")) {
		snprintf(b, n,
			 "{\"type\":\"co2\",\"form\":\"S2\",\"x\":%d,\"w\":30,\"align\":\"%s\","
			 "\"invert\":1000}",
			 x, align);
	} else if (!strcmp(key, "graph")) {
		snprintf(b, n, "{\"type\":\"graph\",\"form\":\"L\",\"x\":%d,\"w\":30,\"hours\":11}",
			 x);
	} else {
		b[0] = '\0';
	}
	return b;
}

/* core.js dayStats() over 16 hours */
static void day_stats(const struct golden_slots *g, int *tmax, int *tmin, int *from, int *to)
{
	*tmax = *tmin = g->ht[0];
	*from = *to = -1;
	for (int i = 0; i < 16; i++) {
		*tmax = g->ht[i] > *tmax ? g->ht[i] : *tmax;
		*tmin = g->ht[i] < *tmin ? g->ht[i] : *tmin;
	}
	for (int i = 0; i < 16; i++) {
		if (g->hpop[i] >= 50) {
			*from = (8 + i) % 24;
			for (int k = i; k < 16; k++) {
				if (g->hpop[k] < 50) {
					*to = (8 + k) % 24;
					break;
				}
			}
			break;
		}
	}
}

ZTEST(screens, test_slot_widgets_match_core_js)
{
	static char json[4096];
	char l[512], r[512], tb[16];
	struct ws_frame f;

	for (int i = 0; i < N(golden_slot_cases); i++) {
		const struct golden_slots *g = &golden_slot_cases[i];
		int tmax, tmin, from, to;

		/* centre: icon + large temperature, centred like screenSlots() */
		ws_big_temp_str(tb, sizeof(tb), g->t * 10, true);
		int tw = ws_text_width(&ws_font_big, tb, 1);
		int cw = 11 + 3 + tw;
		int cx = (WS_W - cw + 1) / 2;

		snprintf(json, sizeof(json),
			 "{\"schema\":1,\"screens\":[{\"id\":\"s\",\"default\":true,\"items\":["
			 "%s,%s,{\"type\":\"icon\",\"x\":%d},"
			 "{\"type\":\"temp\",\"x\":%d,\"w\":%d,\"plus\":true}]}]}",
			 widget_json(g->left, 0, "left", l, sizeof(l)),
			 widget_json(g->right, 72, "right", r, sizeof(r)), cx, cx + 14, tw);
		zassert_ok(compile_cfg(json), "%s", g->name);

		ws_vars_init(&vars);
		ws_vars_set_num(&vars, WS_V_OUT_T, g->t * 10, 0);
		ws_vars_set_num(&vars, WS_V_OUT_COND, ws_cond_parse(g->cond), 0);
		ws_vars_set_num(&vars, WS_V_OUT_WIND, g->wind * 10, 0);
		ws_vars_set_num(&vars, WS_V_OUT_WIND_DIR, ws_dir_parse(g->dir_from), 0);
		ws_vars_set_num(&vars, WS_V_IN_T, g->in_t_deci, 0);
		ws_vars_set_num(&vars, WS_V_IN_RH, g->in_rh * 10, 0);
		ws_vars_set_num(&vars, WS_V_IN_CO2, g->in_co2 * 10, 0);
		ws_vars_set_num(&vars, WS_V_IN_P, g->in_p * 10, 0);
		ws_vars_set_num(&vars, WS_V_IN_P_TREND, g->in_ptrend * 10, 0);
		day_stats(g, &tmax, &tmin, &from, &to);
		ws_vars_set_num(&vars, WS_V_FC_TMAX, tmax * 10, 0);
		ws_vars_set_num(&vars, WS_V_FC_TMIN, tmin * 10, 0);
		ws_vars_set_num(&vars, WS_V_FC_RAIN_FROM, from * 10, 0);
		ws_vars_set_num(&vars, WS_V_FC_RAIN_TO, to * 10, 0);
		struct ws_hours h = {.n = 12};

		for (int k = 0; k < 12; k++) {
			h.t[k] = g->ht[k];
			h.pop[k] = (uint8_t)g->hpop[k];
		}
		ws_vars_set_hours(&vars, &h, 0);

		struct ws_render_ctx rc = {&cfg, &vars, {0, 0}, 0};

		ws_render_screen(&rc, 0, &f);
		if (memcmp(f.bits, g->frame, sizeof(f.bits))) {
			TC_PRINT("%s\n", g->name);
			dump_frame_diff(&f, g->frame);
		}
		zassert_mem_equal(f.bits, g->frame, sizeof(f.bits), "%s", g->name);
	}
}
