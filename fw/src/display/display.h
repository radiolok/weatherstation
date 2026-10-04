/* Sign service: screen selection, rendering and frames to UART1 (F4). */
#ifndef WS_DISPLAY_H_
#define WS_DISPLAY_H_

#include <stdbool.h>
#include <stdint.h>

#include <ws/config.h>
#include <ws/sign.h>

#define WS_DISPLAY_PERIOD_MS 10000 /* rules every 10 s */
#define WS_DISPLAY_REPEAT_MS 30000 /* unchanged frame is repeated every 30 s */

enum ws_sign_pattern {
	WS_PATTERN_NONE, /* normal operation */
	WS_PATTERN_CHECKER,
	WS_PATTERN_COLUMNS,
	WS_PATTERN_ROWS,
	WS_PATTERN_ALL,
	WS_PATTERN_BLANK,
};

struct ws_display_stats {
	uint32_t frames_sent;     /* changed frames */
	uint32_t frames_repeated; /* unchanged repeats */
	uint32_t flips_total;
	uint32_t flips_24h;
	uint32_t last_dots;
	uint32_t last_cols;
	uint32_t tx_bytes;
};

int ws_display_start(void);
/* Re-evaluate now (variables changed, pin, config). */
void ws_display_kick(void);
/* Manual pin: screen index, minutes (0 = until cancelled); -1 = automatic. */
void ws_display_pin(int screen, uint32_t minutes);
/* Shows a screen of an arbitrary config for `seconds` (<= 300) without
 * saving it. `screen_id` NULL: the first screen. */
int ws_display_preview(const char *json, size_t len, const char *screen_id, uint32_t seconds,
		       struct ws_cfg_errors *errs);
void ws_display_pattern(enum ws_sign_pattern p);
int ws_display_pattern_parse(const char *s);
void ws_display_get_stats(struct ws_display_stats *st);
void ws_display_last_frame(struct ws_frame *f);

#endif
