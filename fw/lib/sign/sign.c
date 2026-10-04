/* Frame buffer, drawing primitives and the Mobitec encoder. */
#include <string.h>
#include <stdio.h>

#include <ws/sign.h>

void ws_frame_clear(struct ws_frame *f)
{
	memset(f->bits, 0, sizeof(f->bits));
}

bool ws_frame_get(const struct ws_frame *f, int x, int y)
{
	if (x < 0 || x >= WS_W || y < 0 || y >= WS_H) {
		return false;
	}
	int i = y * WS_W + x;

	return (f->bits[i >> 3] >> (i & 7)) & 1;
}

void ws_frame_set(struct ws_frame *f, int x, int y, bool on)
{
	if (x < 0 || x >= WS_W || y < 0 || y >= WS_H) {
		return;
	}
	int i = y * WS_W + x;

	if (on) {
		f->bits[i >> 3] |= (uint8_t)(1u << (i & 7));
	} else {
		f->bits[i >> 3] &= (uint8_t) ~(1u << (i & 7));
	}
}

void ws_canvas_init(struct ws_canvas *c, struct ws_frame *f)
{
	c->f = f;
	c->clip = (struct ws_rect){0, 0, WS_W, WS_H};
}

static bool in_clip(const struct ws_canvas *c, int x, int y)
{
	return x >= c->clip.x && x < c->clip.x + c->clip.w && y >= c->clip.y &&
	       y < c->clip.y + c->clip.h;
}

void ws_px(struct ws_canvas *c, int x, int y)
{
	if (in_clip(c, x, y)) {
		ws_frame_set(c->f, x, y, true);
	}
}

void ws_blit(struct ws_canvas *c, const struct ws_bitmap *bm, int x, int y)
{
	if (!bm) {
		return;
	}
	for (int dy = 0; dy < bm->h; dy++) {
		uint16_t row = bm->rows[dy];

		for (int dx = 0; dx < bm->w; dx++) {
			if (row & (1u << (bm->w - 1 - dx))) {
				ws_px(c, x + dx, y + dy);
			}
		}
	}
}

void ws_invert(struct ws_canvas *c, int x0, int y0, int x1, int y1)
{
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			if (in_clip(c, x, y) && x >= 0 && x < WS_W && y >= 0 && y < WS_H) {
				ws_frame_set(c->f, x, y, !ws_frame_get(c->f, x, y));
			}
		}
	}
}

void ws_line(struct ws_canvas *c, int x0, int y0, int x1, int y1)
{
	int dx = x1 > x0 ? x1 - x0 : x0 - x1;
	int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int e = dx + dy;

	for (;;) {
		ws_px(c, x0, y0);
		if (x0 == x1 && y0 == y1) {
			break;
		}
		int e2 = 2 * e;

		if (e2 >= dy) {
			e += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			e += dx;
			y0 += sy;
		}
	}
}

int ws_utf8_next(const char *s, uint32_t *cp)
{
	const unsigned char *u = (const unsigned char *)s;

	if (u[0] < 0x80) {
		*cp = u[0];
		return 1;
	}
	if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
		*cp = ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
		return 2;
	}
	if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
		*cp = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) |
		      (u[2] & 0x3F);
		return 3;
	}
	if ((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 &&
	    (u[3] & 0xC0) == 0x80) {
		*cp = ((uint32_t)(u[0] & 0x07) << 18) | ((uint32_t)(u[1] & 0x3F) << 12) |
		      ((uint32_t)(u[2] & 0x3F) << 6) | (u[3] & 0x3F);
		return 4;
	}
	*cp = 0xFFFD;
	return 1;
}

const struct ws_bitmap *ws_font_glyph(const struct ws_font *font, uint32_t cp)
{
	size_t lo = 0, hi = font->count;

	while (lo < hi) {
		size_t mid = (lo + hi) / 2;

		if (font->glyphs[mid].cp == cp) {
			return &font->glyphs[mid].bm;
		}
		if (font->glyphs[mid].cp < cp) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return NULL;
}

int ws_text_width(const struct ws_font *font, const char *s, int gap)
{
	int w = 0;

	while (*s) {
		uint32_t cp;

		s += ws_utf8_next(s, &cp);
		const struct ws_bitmap *g = ws_font_glyph(font, cp);

		if (g) {
			w += g->w + gap;
		}
	}
	w -= gap;
	return w > 0 ? w : 0;
}

int ws_text(struct ws_canvas *c, const struct ws_font *font, const char *s, int x, int y, int gap)
{
	while (*s) {
		uint32_t cp;

		s += ws_utf8_next(s, &cp);
		const struct ws_bitmap *g = ws_font_glyph(font, cp);

		if (!g) {
			continue;
		}
		ws_blit(c, g, x, y);
		x += g->w + gap;
	}
	return x - gap;
}

int ws_round_deci(int32_t deci)
{
	/* Math.round(deci / 10): floor(v + 0.5) */
	int32_t q = deci + 5;

	return q >= 0 ? q / 10 : -((-q + 9) / 10);
}

void ws_temp_str(char *buf, size_t len, int32_t deci)
{
	int v = ws_round_deci(deci);

	snprintf(buf, len, "%s%d", v > 0 ? "+" : (v < 0 ? "-" : ""), v < 0 ? -v : v);
}

void ws_big_temp_str(char *buf, size_t len, int32_t deci, bool with_plus)
{
	int v = ws_round_deci(deci);

	snprintf(buf, len, "%s%d\xC2\xB0", v < 0 ? "-" : (v > 0 && with_plus ? "+" : ""),
		 v < 0 ? -v : v);
}

size_t ws_mobitec_encode(const struct ws_frame *f, uint8_t addr, uint8_t *out, size_t out_len)
{
	uint8_t m[WS_MOBITEC_MAX];
	size_t n = 0;

	m[n++] = 0xFF;
	m[n++] = addr;
	m[n++] = 0xA2;
	for (int band = 0; band < (WS_H + 3) / 4; band++) {
		m[n++] = 0xD2;
		m[n++] = 0x00;
		m[n++] = 0xD3;
		m[n++] = (uint8_t)(band * 4 + 4);
		m[n++] = 0xD4;
		m[n++] = 0x77;
		for (int x = 0; x < WS_W; x++) {
			uint8_t v = 0;

			for (int l = 0; l < 4; l++) {
				int y = band * 4 + l;

				if (y < WS_H && ws_frame_get(f, x, y)) {
					v |= (uint8_t)(1u << l);
				}
			}
			m[n++] = 0x20 + v;
		}
	}
	unsigned int cs = 0;

	for (size_t i = 1; i < n; i++) {
		cs += m[i];
	}
	cs &= 0xFF;
	m[n++] = (uint8_t)cs;
	if (cs == 0xFE) {
		m[n++] = 0x00;
	} else if (cs == 0xFF) {
		m[n - 1] = 0xFE;
		m[n++] = 0x01;
	}
	m[n++] = 0xFF;
	if (n > out_len) {
		return 0;
	}
	memcpy(out, m, n);
	return n;
}

int ws_mobitec_decode(const uint8_t *in, size_t len, struct ws_frame *f, uint8_t *addr)
{
	size_t i = 3;
	unsigned int cs = 0;
	int x = 0, y = 0;

	if (len < 5 || in[0] != 0xFF || in[2] != 0xA2 || in[len - 1] != 0xFF) {
		return -1;
	}
	ws_frame_clear(f);
	if (addr) {
		*addr = in[1];
	}
	cs = in[1] + in[2];
	while (i < len - 1) {
		uint8_t b = in[i];

		if (b == 0xD2 || b == 0xD3 || b == 0xD4) {
			if (i + 1 >= len - 1) {
				return -2;
			}
			if (b == 0xD2) {
				x = in[i + 1];
			} else if (b == 0xD3) {
				y = in[i + 1] - 4; /* y is the bottom row of the band + 1 */
			} else if (in[i + 1] != 0x77) {
				return -3; /* only the bitmap "font" is supported */
			}
			cs += b + in[i + 1];
			i += 2;
		} else if (b >= 0x20 && b <= 0x2F) {
			for (int l = 0; l < 4; l++) {
				if ((b - 0x20) & (1 << l)) {
					ws_frame_set(f, x, y + l, true);
				}
			}
			cs += b;
			x++;
			i++;
		} else {
			break; /* checksum */
		}
	}
	/* Checksum: one byte, or FE 00 (=FE) / FE 01 (=FF). */
	size_t rest = len - 1 - i;
	unsigned int got;

	if (rest == 1) {
		got = in[i];
	} else if (rest == 2 && in[i] == 0xFE && in[i + 1] <= 1) {
		got = 0xFE + in[i + 1];
	} else {
		return -4;
	}
	return (cs & 0xFF) == got ? 0 : -5;
}

struct ws_frame_diff ws_frame_diff(const struct ws_frame *a, const struct ws_frame *b)
{
	struct ws_frame_diff d = {0, 0};

	for (int x = 0; x < WS_W; x++) {
		bool col = false;

		for (int y = 0; y < WS_H; y++) {
			if (ws_frame_get(a, x, y) != ws_frame_get(b, x, y)) {
				d.dots++;
				col = true;
			}
		}
		d.cols += col;
	}
	return d;
}
