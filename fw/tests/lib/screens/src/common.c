#include <string.h>
#include <zephyr/ztest.h>

#include "common.h"

struct ws_config cfg;
struct ws_vars vars;
struct ws_cfg_errors errs;
static struct ws_jtok toks[WS_JSON_TOKENS];

int compile_cfg(const char *json)
{
	int r = ws_cfg_compile(json, strlen(json), &cfg, &errs, toks, WS_JSON_TOKENS);

	for (int i = 0; r && i < errs.count && i < WS_MAX_ERRORS; i++) {
		TC_PRINT("  %s: %s\n", errs.e[i].path, errs.e[i].msg);
	}
	return r;
}

int compile_factory(void)
{
	return ws_cfg_compile((const char *)ws_factory_screens_json, ws_factory_screens_json_len,
			      &cfg, &errs, toks, WS_JSON_TOKENS);
}

void set_clock(int64_t mono, int hour, int min, int dow)
{
	ws_vars_set_num(&vars, WS_V_TIME_HOUR, hour * 10, mono);
	ws_vars_set_num(&vars, WS_V_TIME_MIN, min * 10, mono);
	ws_vars_set_num(&vars, WS_V_TIME_DOW, dow * 10, mono);
}

int screen_id(const char *id)
{
	return ws_cfg_screen_by_id(&cfg, id);
}

void dump_frame_diff(const struct ws_frame *got, const uint8_t *want)
{
	for (int y = 0; y < WS_H; y++) {
		for (int x = 0; x < WS_W; x++) {
			int i = y * WS_W + x;
			bool w = want ? (want[i >> 3] >> (i & 7)) & 1 : false;
			bool g = ws_frame_get(got, x, y);

			TC_PRINT("%c", g == w ? (g ? '#' : '.') : (g ? '+' : '-'));
		}
		TC_PRINT("\n");
	}
}
