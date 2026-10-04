/* Glyph tables generated from tools/sign-simulator/core.js. */
#include <string.h>

#include <ws/sign.h>

struct ws_picto_builtin {
	const char *name;
	struct ws_bitmap l;
	struct ws_bitmap s;
};

#include "glyphs_gen.h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

const struct ws_font ws_font_big = {ws_big_glyphs, ARRAY_LEN(ws_big_glyphs), 11};
const struct ws_font ws_font_small = {ws_small_glyphs, ARRAY_LEN(ws_small_glyphs), 5};

static const char *const cond_names[WS_COND_COUNT] = {
	"clear", "night", "pcloud", "pcloud_n", "cloudy", "rain", "snow", "storm",
};

static const char *const dir_names[WS_DIR_COUNT] = {
	"n", "ne", "e", "se", "s", "sw", "w", "nw",
};

const char *ws_cond_name(enum ws_cond c)
{
	return (unsigned int)c < WS_COND_COUNT ? cond_names[c] : "";
}

int ws_cond_parse(const char *s)
{
	for (int i = 0; i < WS_COND_COUNT; i++) {
		if (strcmp(s, cond_names[i]) == 0) {
			return i;
		}
	}
	return -1;
}

const char *ws_dir_name(enum ws_dir d)
{
	return (unsigned int)d < WS_DIR_COUNT ? dir_names[d] : "";
}

int ws_dir_parse(const char *s)
{
	for (int i = 0; i < WS_DIR_COUNT; i++) {
		if (strcmp(s, dir_names[i]) == 0) {
			return i;
		}
	}
	return -1;
}

const struct ws_bitmap *ws_icon(enum ws_cond c, bool large)
{
	if ((unsigned int)c >= WS_COND_COUNT) {
		return NULL;
	}
	return large ? &ws_icon_l[c] : &ws_icon_s[c];
}

const struct ws_bitmap *ws_wind_arrow(enum ws_dir to)
{
	return (unsigned int)to < WS_DIR_COUNT ? &ws_wind_arrows[to] : NULL;
}

const struct ws_bitmap *ws_trend_arrow(enum ws_trend t)
{
	return (unsigned int)t < ARRAY_LEN(ws_trend_arrows) ? &ws_trend_arrows[t] : NULL;
}

const struct ws_bitmap *ws_picto_builtin(const char *name, bool large)
{
	for (size_t i = 0; i < ARRAY_LEN(ws_pictos); i++) {
		if (strcmp(ws_pictos[i].name, name) == 0) {
			return large ? &ws_pictos[i].l : &ws_pictos[i].s;
		}
	}
	return NULL;
}

size_t ws_picto_builtin_count(void)
{
	return ARRAY_LEN(ws_pictos);
}

const char *ws_picto_builtin_name(size_t idx)
{
	return idx < ARRAY_LEN(ws_pictos) ? ws_pictos[idx].name : NULL;
}
