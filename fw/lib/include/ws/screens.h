/*
 * Screen engine: element renderer and screen selection rules
 * (docs/screen-constructor.md, 4-6).
 *
 * The renderer is mirrored in fw/web/src/render.js; golden frames keep the
 * two implementations identical to the dot.
 */
#ifndef WS_SCREENS_H_
#define WS_SCREENS_H_

#include <stdbool.h>
#include <stdint.h>

#include <ws/config.h>
#include <ws/sign.h>
#include <ws/vars.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ws_render_ctx {
	struct ws_config *cfg; /* conditions keep hysteresis state */
	const struct ws_vars *vars;
	struct ws_now now;
	int32_t sec_of_day; /* local time for rotators, -1 if unknown (uptime is used) */
};

void ws_render_screen(struct ws_render_ctx *rc, int screen, struct ws_frame *out);

/* Draws one item (with alternatives and rotators) into `f`. */
void ws_render_item(struct ws_render_ctx *rc, const struct ws_item *it, struct ws_frame *f);

enum ws_item_state {
	WS_ITEM_OK,
	WS_ITEM_EMPTY,   /* known, nothing to show (no rain) */
	WS_ITEM_UNKNOWN, /* value unknown: drawn as "--", skipped in rotators */
};

enum ws_item_state ws_item_state(struct ws_render_ctx *rc, const struct ws_item *it);

/* Which child of a rotator is shown now, -1 if none is available. */
int ws_rotator_pick(struct ws_render_ctx *rc, const struct ws_item *rot);

/* Evaluates a condition with hysteresis (updates the comparison state).
 * `why` receives the first comparison that decided the result, e.g.
 * "in.co2=1032 > 1000". Comparisons with unknown variables are false. */
bool ws_cond_eval(struct ws_config *cfg, const struct ws_condition *c, const struct ws_vars *vars,
		  const struct ws_now *now, char *why, size_t why_len);

/* ---- screen selection (section 6) ---- */

#define WS_DEBUG_PIN_S 60

struct ws_screen_state {
	bool raw;    /* condition and time window true now (with hysteresis) */
	bool active; /* after on/off delays */
	int64_t raw_since;
	int64_t false_since;
	int64_t active_since;
	char why[64];
};

struct ws_engine {
	struct ws_config *cfg;
	struct ws_screen_state st[WS_MAX_SCREENS];
	int current;
	int64_t current_since;
	int pinned;        /* -1: automatic */
	int64_t pin_until; /* 0: until cancelled */
	bool pin_debug;    /* pinned by the service button */
	char reason[96];
	int last_candidates; /* bit mask of candidates at the last step */
};

void ws_engine_init(struct ws_engine *e, struct ws_config *cfg, int64_t mono);

/* Re-evaluates the rules. Time window needs time.hour / time.min / time.dow
 * in `vars`. Returns true when the current screen changed. */
bool ws_engine_step(struct ws_engine *e, const struct ws_vars *vars, const struct ws_now *now);

/* Pins a screen for `minutes` (0 = until cancelled); screen -1 cancels. */
void ws_engine_pin(struct ws_engine *e, int screen, uint32_t minutes, int64_t mono);
/* Short button press: next screen in the list (disabled ones too) for 60 s. */
void ws_engine_debug_next(struct ws_engine *e, int64_t mono);

/* After a new configuration: keep the current screen by id if it still
 * exists, drop the pin otherwise. */
void ws_engine_reconfigure(struct ws_engine *e, struct ws_config *cfg, int64_t mono);

/* Is the time inside the rule window (minutes of day, day of week 1..7)? */
bool ws_rule_time_ok(const struct ws_rule *r, int minute_of_day, int dow);

#ifdef __cplusplus
}
#endif

#endif /* WS_SCREENS_H_ */
