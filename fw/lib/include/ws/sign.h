/*
 * Sign core: 102x11 frame buffer, glyphs and the Mobitec frame encoder.
 *
 * Pure C, no Zephyr dependencies. Drawing semantics follow
 * tools/sign-simulator/core.js one to one: the golden tests compare
 * both renderers bit for bit.
 */
#ifndef WS_SIGN_H_
#define WS_SIGN_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS_W           102
#define WS_H           11
#define WS_FRAME_BYTES ((WS_W * WS_H + 7) / 8)

/* Mobitec frame: header 3 + 3 bands * (6 + 102) + checksum 2 + end 1. */
#define WS_MOBITEC_MAX  330
#define WS_MOBITEC_ADDR 0x06

/* Frame buffer, bit (y * WS_W + x), LSB first within a byte. */
struct ws_frame {
	uint8_t bits[WS_FRAME_BYTES];
};

/* Monochrome bitmap up to 16x11, one row per uint16, bit (w - 1 - x). */
struct ws_bitmap {
	uint8_t w;
	uint8_t h;
	uint16_t rows[WS_H];
};

struct ws_glyph {
	uint32_t cp;
	struct ws_bitmap bm;
};

struct ws_font {
	const struct ws_glyph *glyphs;
	size_t count;
	uint8_t height;
};

extern const struct ws_font ws_font_big;   /* digits 6x11 */
extern const struct ws_font ws_font_small; /* 3x5, Cyrillic capitals, signs */

/* Clip rectangle; drawing outside it (or outside the frame) is dropped. */
struct ws_rect {
	int16_t x, y, w, h;
};

/* Drawing target: a frame plus the active clip rectangle. */
struct ws_canvas {
	struct ws_frame *f;
	struct ws_rect clip;
};

void ws_frame_clear(struct ws_frame *f);
bool ws_frame_get(const struct ws_frame *f, int x, int y);
void ws_frame_set(struct ws_frame *f, int x, int y, bool on);

/* Canvas covering the whole frame. */
void ws_canvas_init(struct ws_canvas *c, struct ws_frame *f);

void ws_px(struct ws_canvas *c, int x, int y);
void ws_blit(struct ws_canvas *c, const struct ws_bitmap *bm, int x, int y);
void ws_invert(struct ws_canvas *c, int x0, int y0, int x1, int y1);
void ws_line(struct ws_canvas *c, int x0, int y0, int x1, int y1);

/* Decode one UTF-8 code point, returns bytes consumed (>= 1). */
int ws_utf8_next(const char *s, uint32_t *cp);

const struct ws_bitmap *ws_font_glyph(const struct ws_font *font, uint32_t cp);

/* Width of a UTF-8 string, glyphs separated by `gap` columns; unknown
 * characters are skipped like in core.js. */
int ws_text_width(const struct ws_font *font, const char *s, int gap);

/* Draws text at (x, y); returns the x after the last glyph minus gap. */
int ws_text(struct ws_canvas *c, const struct ws_font *font, const char *s, int x, int y, int gap);

/* core.js helpers. Values are in tenths (deci-degrees) and rounded like
 * JavaScript Math.round. */
int ws_round_deci(int32_t deci);
/* "+12" / "-7" / "0" */
void ws_temp_str(char *buf, size_t len, int32_t deci);
/* "+12°" (plus only when with_plus), "-7°", "0°" */
void ws_big_temp_str(char *buf, size_t len, int32_t deci, bool with_plus);

/* Mobitec encoder (protocol from legacy/rpi/scripts/futaba.py). Writes the
 * whole frame as three bitmap bands through font 0x77. Returns length. */
size_t ws_mobitec_encode(const struct ws_frame *f, uint8_t addr, uint8_t *out, size_t out_len);

/* Decoder for tests and the RS-485 sniffer: returns 0 on success. */
int ws_mobitec_decode(const uint8_t *in, size_t len, struct ws_frame *f, uint8_t *addr);

struct ws_frame_diff {
	int dots; /* flipped dots */
	int cols; /* columns with at least one flipped dot */
};

struct ws_frame_diff ws_frame_diff(const struct ws_frame *a, const struct ws_frame *b);

/* Splits a raw RS-485 byte stream into Mobitec frames (FF ... FF). Used by
 * the bench sniffer (tools/sign-sniffer). 0xFF only appears as the first and
 * the last byte of a frame (the checksum is escaped), so a frame is every
 * FF ... FF run longer than the header. Bytes outside frames are counted as
 * garbage. */
struct ws_mobitec_rx {
	uint8_t buf[WS_MOBITEC_MAX + 8];
	size_t n;         /* bytes of the frame being received */
	size_t len;       /* length of the last complete frame in buf */
	uint32_t garbage; /* bytes outside frames */
	uint32_t overflows;
};

enum ws_mobitec_rx_result {
	WS_RX_NONE,     /* need more bytes */
	WS_RX_FRAME,    /* buf[0..len) holds a complete FF ... FF frame */
	WS_RX_OVERFLOW, /* frame longer than WS_MOBITEC_MAX + 8 was dropped */
};

void ws_mobitec_rx_init(struct ws_mobitec_rx *rx);
enum ws_mobitec_rx_result ws_mobitec_rx_feed(struct ws_mobitec_rx *rx, uint8_t b);

/* Named bitmaps from tools/sign-simulator/core.js. */
enum ws_cond {
	WS_COND_CLEAR,
	WS_COND_NIGHT,
	WS_COND_PCLOUD,
	WS_COND_PCLOUD_N,
	WS_COND_CLOUDY,
	WS_COND_RAIN,
	WS_COND_SNOW,
	WS_COND_STORM,
	WS_COND_COUNT
};

enum ws_dir {
	WS_DIR_N,
	WS_DIR_NE,
	WS_DIR_E,
	WS_DIR_SE,
	WS_DIR_S,
	WS_DIR_SW,
	WS_DIR_W,
	WS_DIR_NW,
	WS_DIR_COUNT
};

enum ws_trend {
	WS_TREND_UP,
	WS_TREND_DOWN,
	WS_TREND_FLAT,
};

const char *ws_cond_name(enum ws_cond c);
int ws_cond_parse(const char *s); /* -1 when unknown */
const char *ws_dir_name(enum ws_dir d);
int ws_dir_parse(const char *s); /* -1 when unknown */

const struct ws_bitmap *ws_icon(enum ws_cond c, bool large);
/* Arrow pointing where the wind blows. */
const struct ws_bitmap *ws_wind_arrow(enum ws_dir to);
static inline enum ws_dir ws_dir_opposite(enum ws_dir d)
{
	return (enum ws_dir)((d + 4) % WS_DIR_COUNT);
}
const struct ws_bitmap *ws_trend_arrow(enum ws_trend t);

/* Built-in pictogram by name, NULL if none. */
const struct ws_bitmap *ws_picto_builtin(const char *name, bool large);
size_t ws_picto_builtin_count(void);
const char *ws_picto_builtin_name(size_t idx);

#ifdef __cplusplus
}
#endif

#endif /* WS_SIGN_H_ */
