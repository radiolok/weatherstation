/* Sign core: glyphs, primitives and Mobitec encoder against core.js and futaba.py. */
#include <string.h>
#include <zephyr/ztest.h>

#include <ws/sign.h>

#include "sign_golden_gen.h"
#include "futaba_gen.h"

#define N(a) (int)(sizeof(a) / sizeof((a)[0]))

static const struct ws_bitmap *named(const char *kind, const char *name)
{
	if (!strcmp(kind, "icon_l")) {
		return ws_icon(ws_cond_parse(name), true);
	}
	if (!strcmp(kind, "icon_s")) {
		return ws_icon(ws_cond_parse(name), false);
	}
	if (!strcmp(kind, "wind")) {
		return ws_wind_arrow(ws_dir_parse(name));
	}
	if (!strcmp(kind, "trend")) {
		return ws_trend_arrow(!strcmp(name, "up")     ? WS_TREND_UP
				      : !strcmp(name, "down") ? WS_TREND_DOWN
							      : WS_TREND_FLAT);
	}
	if (!strcmp(kind, "picto_l")) {
		return ws_picto_builtin(name, true);
	}
	if (!strcmp(kind, "picto_s")) {
		return ws_picto_builtin(name, false);
	}
	return NULL;
}

static void replay(struct ws_canvas *c, const struct golden_op *op)
{
	char buf[16];

	switch (op->kind) {
	case G_TEXT_BIG:
		ws_text(c, &ws_font_big, op->s, op->a, op->b, 1);
		break;
	case G_TEXT_SMALL:
		ws_text(c, &ws_font_small, op->s, op->a, op->b, 1);
		break;
	case G_BLIT: {
		const struct ws_bitmap *bm = named(op->s, op->s2);

		zassert_not_null(bm, "%s:%s", op->s, op->s2);
		ws_blit(c, bm, op->a, op->b);
		break;
	}
	case G_BIGTEMP:
		ws_big_temp_str(buf, sizeof(buf), op->a, op->c);
		ws_text(c, &ws_font_big, buf, op->b, 0, 1);
		break;
	case G_INVERT:
		ws_invert(c, op->a, op->b, op->c, op->d);
		break;
	case G_LINE:
		ws_line(c, op->a, op->b, op->c, op->d);
		break;
	}
}

static void dump_diff(const struct ws_frame *got, const uint8_t *want)
{
	for (int y = 0; y < WS_H; y++) {
		for (int x = 0; x < WS_W; x++) {
			int i = y * WS_W + x;
			bool w = (want[i >> 3] >> (i & 7)) & 1;
			bool g = ws_frame_get(got, x, y);

			TC_PRINT("%c", g == w ? (g ? '#' : '.') : (g ? '+' : '-'));
		}
		TC_PRINT("\n");
	}
}

ZTEST(sign, test_golden_ops)
{
	static struct ws_frame f;
	struct ws_canvas c;
	uint8_t mob[WS_MOBITEC_MAX];

	for (int i = 0; i < N(golden_cases); i++) {
		const struct golden_case *gc = &golden_cases[i];

		ws_frame_clear(&f);
		ws_canvas_init(&c, &f);
		for (int k = 0; k < gc->n_ops; k++) {
			replay(&c, &gc->ops[k]);
		}
		if (memcmp(f.bits, gc->frame, sizeof(f.bits)) != 0) {
			dump_diff(&f, gc->frame);
		}
		zassert_mem_equal(f.bits, gc->frame, sizeof(f.bits), "frame %s", gc->name);
		size_t n = ws_mobitec_encode(&f, WS_MOBITEC_ADDR, mob, sizeof(mob));

		zassert_equal(n, gc->mob_len, "len %s", gc->name);
		zassert_mem_equal(mob, gc->mob, n, "bytes %s", gc->name);
	}
}

ZTEST(sign, test_checksum_escapes)
{
	uint8_t mob[WS_MOBITEC_MAX];
	bool seen_fe = false, seen_ff = false;

	for (int i = 0; i < N(golden_raws); i++) {
		struct ws_frame f;

		memcpy(f.bits, golden_raws[i].frame, sizeof(f.bits));
		size_t n = ws_mobitec_encode(&f, WS_MOBITEC_ADDR, mob, sizeof(mob));

		zassert_equal(n, golden_raws[i].mob_len, "%s", golden_raws[i].name);
		zassert_mem_equal(mob, golden_raws[i].mob, n, "%s", golden_raws[i].name);
		if (!strcmp(golden_raws[i].name, "checksum_fe")) {
			/* FE is followed by 00, then the FF terminator */
			zassert_equal(mob[n - 3], 0xFE);
			zassert_equal(mob[n - 2], 0x00);
			seen_fe = true;
		}
		if (!strcmp(golden_raws[i].name, "checksum_ff")) {
			/* FF is sent as FE 01 */
			zassert_equal(mob[n - 3], 0xFE);
			zassert_equal(mob[n - 2], 0x01);
			seen_ff = true;
		}
		zassert_equal(mob[n - 1], 0xFF);
	}
	zassert_true(seen_fe && seen_ff);
}

ZTEST(sign, test_futaba_reference)
{
	uint8_t mob[WS_MOBITEC_MAX];

	for (int i = 0; i < N(futaba_cases); i++) {
		struct ws_frame f;

		memcpy(f.bits, futaba_cases[i].frame, sizeof(f.bits));
		size_t n = ws_mobitec_encode(&f, WS_MOBITEC_ADDR, mob, sizeof(mob));

		zassert_equal(n, futaba_cases[i].len, "%s", futaba_cases[i].name);
		zassert_mem_equal(mob, futaba_cases[i].bytes, n, "%s", futaba_cases[i].name);
	}
}

ZTEST(sign, test_decode_roundtrip)
{
	uint8_t mob[WS_MOBITEC_MAX];

	for (int i = 0; i < N(golden_raws); i++) {
		struct ws_frame f, g;
		uint8_t addr = 0;

		memcpy(f.bits, golden_raws[i].frame, sizeof(f.bits));
		size_t n = ws_mobitec_encode(&f, 0x06, mob, sizeof(mob));

		zassert_ok(ws_mobitec_decode(mob, n, &g, &addr), "%s", golden_raws[i].name);
		zassert_equal(addr, 0x06);
		zassert_mem_equal(f.bits, g.bits, sizeof(f.bits));
		mob[10] ^= 0x01; /* corrupt one data byte */
		zassert_not_equal(ws_mobitec_decode(mob, n, &g, NULL), 0);
	}
}

ZTEST(sign, test_encode_small_buffer)
{
	struct ws_frame f;
	uint8_t mob[100];

	ws_frame_clear(&f);
	zassert_equal(ws_mobitec_encode(&f, 6, mob, sizeof(mob)), 0);
}

ZTEST(sign, test_diff_counts)
{
	struct ws_frame a, b;

	ws_frame_clear(&a);
	ws_frame_clear(&b);
	struct ws_frame_diff d = ws_frame_diff(&a, &b);

	zassert_equal(d.dots, 0);
	zassert_equal(d.cols, 0);
	ws_frame_set(&b, 0, 0, true);
	ws_frame_set(&b, 0, 10, true);
	ws_frame_set(&b, 101, 5, true);
	d = ws_frame_diff(&a, &b);
	zassert_equal(d.dots, 3);
	zassert_equal(d.cols, 2);
}

ZTEST(sign, test_clip_rect)
{
	struct ws_frame f;
	struct ws_canvas c;

	ws_frame_clear(&f);
	ws_canvas_init(&c, &f);
	c.clip = (struct ws_rect){10, 2, 5, 3};
	ws_invert(&c, 0, 0, 101, 10);
	struct ws_frame_diff d;
	struct ws_frame e;

	ws_frame_clear(&e);
	d = ws_frame_diff(&f, &e);
	zassert_equal(d.dots, 15);
	zassert_equal(d.cols, 5);
	zassert_true(ws_frame_get(&f, 10, 2));
	zassert_false(ws_frame_get(&f, 15, 2));
}

ZTEST(sign, test_round_like_js)
{
	/* Math.round: halves go up, also for negatives */
	zassert_equal(ws_round_deci(25), 3);
	zassert_equal(ws_round_deci(-25), -2);
	zassert_equal(ws_round_deci(-26), -3);
	zassert_equal(ws_round_deci(-4), 0);
	zassert_equal(ws_round_deci(-5), 0);
	zassert_equal(ws_round_deci(-6), -1);
	char buf[12];

	ws_temp_str(buf, sizeof(buf), -4);
	zassert_str_equal(buf, "0");
	ws_temp_str(buf, sizeof(buf), 40);
	zassert_str_equal(buf, "+4");
	ws_big_temp_str(buf, sizeof(buf), 120, true);
	zassert_str_equal(buf, "+12\xC2\xB0");
}

ZTEST(sign, test_text_metrics)
{
	/* "-35°" in the 6x11 font is the default width of a large temperature */
	zassert_equal(ws_text_width(&ws_font_big, "-35\xC2\xB0", 1), 24);
	zassert_equal(ws_text_width(&ws_font_small, "", 1), 0);
	zassert_equal(ws_text_width(&ws_font_small, "\xE2\x98\x82", 1), 5);     /* umbrella */
	zassert_equal(ws_text_width(&ws_font_small, "\xD0\x94\xD0\x9E", 1), 9); /* "ДО" */
	zassert_not_null(ws_picto_builtin("skid", true));
	zassert_is_null(ws_picto_builtin("nope", true));
	zassert_equal(ws_cond_parse("pcloud_n"), WS_COND_PCLOUD_N);
	zassert_equal(ws_cond_parse("fog"), -1);
	zassert_equal(ws_dir_opposite(WS_DIR_NW), WS_DIR_SE);
}

ZTEST_SUITE(sign, NULL, NULL, NULL, NULL, NULL);
