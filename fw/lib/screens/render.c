/*
 * Element renderer. Mirrors fw/web/src/render.js; for the slot widgets it
 * draws exactly what tools/sign-simulator/core.js draws.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <ws/screens.h>

#define UMBRELLA "\xE2\x98\x82"      /* ☂ */
#define DROP     "\xE2\x97\x8A"      /* ◊ */
#define UP       "\xE2\x86\x91"      /* ↑ */
#define DOWN     "\xE2\x86\x93"      /* ↓ */
#define H_RU     "\xD0\xA7"          /* Ч */
#define DO_RU    "\xD0\x94\xD0\x9E"  /* ДО */
#define MS_RU    "\xD0\x9C/\xD0\xA1" /* М/С */
#define MM_RU    "\xD0\x9C\xD0\x9C"  /* ММ */
#define DEG      "\xC2\xB0"

static int floor_div(int a, int b)
{
	int q = a / b;

	if ((a % b != 0) && ((a < 0) != (b < 0))) {
		q--;
	}
	return q;
}

static bool get(struct ws_render_ctx *rc, int var, struct ws_value *v)
{
	if (var == WS_V_NONE) {
		v->known = false;
		return false;
	}
	return ws_vars_get(rc->vars, var, &rc->now, v);
}

static const struct ws_font *font_of(const struct ws_item *it)
{
	return it->form == WS_FORM_L ? &ws_font_big : &ws_font_small;
}

static int align_x(const struct ws_item *it, int tw)
{
	switch (it->align) {
	case WS_ALIGN_RIGHT:
		return it->x + it->w - tw;
	case WS_ALIGN_CENTER:
		return it->x + floor_div(it->w - tw, 2);
	default:
		return it->x;
	}
}

/* core.js put(): text aligned in the frame */
static int put(struct ws_canvas *c, const struct ws_item *it, const struct ws_font *font,
	       const char *s, int y)
{
	int x = align_x(it, ws_text_width(font, s, 1));

	ws_text(c, font, s, x, y, 1);
	return x;
}

/* core.js putIcon(): icon, 2 columns gap, text; aligned as a whole */
static void put_icon(struct ws_canvas *c, const struct ws_item *it, const struct ws_bitmap *bm,
		     const struct ws_font *font, const char *s, int y)
{
	int bw = bm ? bm->w : 0;
	int x = align_x(it, bw + 2 + ws_text_width(font, s, 1));

	ws_blit(c, bm, x, y);
	ws_text(c, font, s, x + bw + 2, y, 1);
}

static void dash(struct ws_canvas *c, const struct ws_item *it)
{
	bool small_icon =
		(it->type == WS_IT_ICON || it->type == WS_IT_PICTO) && it->form == WS_FORM_S;

	put(c, it, font_of(it), small_icon ? "-" : "--", it->y);
}

static void fmt_int(char *b, size_t n, int v)
{
	snprintf(b, n, "%d", v);
}

/* tenths -> "12.5" / "-0.4" */
static void fmt_tenths(char *b, size_t n, int32_t deci)
{
	int32_t a = deci < 0 ? -deci : deci;

	snprintf(b, n, "%s%ld.%ld", deci < 0 ? "-" : "", (long)(a / 10), (long)(a % 10));
}

static void draw_temp(struct ws_canvas *c, const struct ws_item *it, int32_t v)
{
	const struct ws_font *font = font_of(it);
	char num[16], s[24];
	bool neg, pos;

	if (it->flags & WS_F_TENTHS) {
		int32_t a = v < 0 ? -v : v;

		snprintf(num, sizeof(num), "%ld.%ld", (long)(a / 10), (long)(a % 10));
		neg = v < 0;
		pos = v > 0;
	} else {
		int r = ws_round_deci(v);

		fmt_int(num, sizeof(num), r < 0 ? -r : r);
		neg = r < 0;
		pos = r > 0;
	}
	const char *deg = (it->flags & WS_F_DEG) ? DEG : "";

	snprintf(s, sizeof(s), "%s%s%s", neg ? "-" : (pos && (it->flags & WS_F_PLUS) ? "+" : ""),
		 num, deg);
	/* "+" is dropped when it does not fit, like core.js bigTemp() */
	if (pos && (it->flags & WS_F_PLUS) && ws_text_width(font, s, 1) > it->w) {
		snprintf(s, sizeof(s), "%s%s", num, deg);
	}
	put(c, it, font, s, it->y);
}

static void draw_number(struct ws_render_ctx *rc, struct ws_canvas *c, const struct ws_item *it,
			const struct ws_value *v)
{
	const struct ws_font *font = font_of(it);
	char num[32], s[64];

	if (v->type == WS_VT_STR) {
		snprintf(num, sizeof(num), "%s", v->str);
	} else if (it->decimals == 0) {
		fmt_int(num, sizeof(num), ws_round_deci(v->num));
	} else {
		fmt_tenths(num, sizeof(num), v->num);
		if (it->decimals == 2) {
			strncat(num, "0", sizeof(num) - strlen(num) - 1);
		}
	}
	snprintf(s, sizeof(s), "%s%s%s", ws_cfg_str(rc->cfg, it->prefix), num,
		 ws_cfg_str(rc->cfg, it->suffix));
	if (it->picto != WS_NO_IDX) {
		put_icon(c, it,
			 ws_cfg_picto(rc->cfg, ws_cfg_str(rc->cfg, it->picto),
				      it->form == WS_FORM_L),
			 font, s, it->y);
	} else {
		put(c, it, font, s, it->y);
	}
}

static void draw_rain(struct ws_render_ctx *rc, struct ws_canvas *c, const struct ws_item *it,
		      int32_t from)
{
	struct ws_value to;
	char s[32];
	bool has_to = get(rc, it->var2, &to) && to.num >= 0;
	const char *none = ws_cfg_str(rc->cfg, it->text);

	if (it->form == WS_FORM_L) {
		if (from < 0) {
			return; /* nothing to say in large digits */
		}
		if (has_to) {
			snprintf(s, sizeof(s), "%02d-%02d", (int)(from / 10), (int)(to.num / 10));
		} else {
			snprintf(s, sizeof(s), "%02d", (int)(from / 10));
		}
		put_icon(c, it, ws_picto_builtin("umbrella", true), &ws_font_big, s, it->y);
		return;
	}
	const struct ws_bitmap *umb = ws_font_glyph(&ws_font_small, 0x2602);

	if (from < 0) {
		put_icon(c, it, umb, &ws_font_small, none, it->y);
		if (it->form == WS_FORM_S2) {
			snprintf(s, sizeof(s), "%u" H_RU, it->hours);
			put(c, it, &ws_font_small, s, it->y + 6);
		}
		return;
	}
	snprintf(s, sizeof(s), "%02d" H_RU, (int)(from / 10));
	put_icon(c, it, umb, &ws_font_small, s, it->y);
	if (it->form == WS_FORM_S2 && has_to) {
		snprintf(s, sizeof(s), DO_RU " %02d" H_RU, (int)(to.num / 10));
		put(c, it, &ws_font_small, s, it->y + 6);
	}
}

static void draw_wind(struct ws_render_ctx *rc, struct ws_canvas *c, const struct ws_item *it,
		      int32_t speed)
{
	struct ws_value dir;
	char s[16];

	fmt_int(s, sizeof(s), ws_round_deci(speed));
	if (get(rc, it->var2, &dir)) {
		enum ws_dir d = (enum ws_dir)dir.num;
		const struct ws_bitmap *arrow =
			ws_wind_arrow((it->flags & WS_F_DIR_FROM) ? d : ws_dir_opposite(d));

		put_icon(c, it, arrow, &ws_font_small, s, it->y);
	} else {
		put(c, it, &ws_font_small, s, it->y);
	}
	if (it->form == WS_FORM_S2) {
		put(c, it, &ws_font_small, MS_RU, it->y + 6);
	}
}

static void temp_str(char *b, size_t n, const char *prefix, int32_t deci)
{
	char t[12];

	ws_temp_str(t, sizeof(t), deci);
	snprintf(b, n, "%s%s", prefix, t);
}

static void draw_pressure(struct ws_render_ctx *rc, struct ws_canvas *c, const struct ws_item *it,
			  int32_t p)
{
	struct ws_value tr;
	enum ws_trend t = WS_TREND_FLAT;
	char s[16];

	if (get(rc, it->var2, &tr)) {
		if (tr.num > it->thr) {
			t = WS_TREND_UP;
		} else if (tr.num < -it->thr) {
			t = WS_TREND_DOWN;
		}
	}
	fmt_int(s, sizeof(s), ws_round_deci(p));
	put_icon(c, it, ws_trend_arrow(t), &ws_font_small, s, it->y);
	if (it->form == WS_FORM_S2) {
		put(c, it, &ws_font_small, MM_RU, it->y + 6);
	}
}

static void draw_co2(struct ws_canvas *c, const struct ws_item *it, int32_t v)
{
	char s[16];
	int ppm = ws_round_deci(v);
	bool inv = it->thr > 0 && v >= it->thr;

	fmt_int(s, sizeof(s), ppm);
	if (it->form == WS_FORM_L) {
		int x = put(c, it, &ws_font_big, s, it->y);

		if (inv) {
			ws_invert(c, x - 1, it->y, x + ws_text_width(&ws_font_big, s, 1),
				  it->y + 10);
		}
		return;
	}
	put(c, it, &ws_font_small, "CO2", it->y);
	int x = put(c, it, &ws_font_small, s, it->y + 6);

	if (inv) {
		ws_invert(c, x - 1, it->y + 5, x + ws_text_width(&ws_font_small, s, 1), it->y + 10);
	}
}

static int graph_y(int t, int mn, int sp)
{
	/* Math.round(7 - (t - mn) / sp * 7) */
	double v = 7.0 - (double)(t - mn) / (double)sp * 7.0;

	return (int)floor(v + 0.5);
}

static void draw_graph(struct ws_canvas *c, const struct ws_item *it, const struct ws_hours *h)
{
	int n = h->n < it->hours ? h->n : it->hours;
	int mx = h->t[0], mn = h->t[0];

	for (int i = 1; i < n; i++) {
		mx = h->t[i] > mx ? h->t[i] : mx;
		mn = h->t[i] < mn ? h->t[i] : mn;
	}
	int sp = mx - mn > 1 ? mx - mn : 1;
	int step = it->w / (n - 1);
	int x = it->x, y0 = it->y;

	if (step < 1) {
		step = 1;
	}
	if (mn < 0 && mx > 0) {
		for (int i = 0; i < it->w; i += 2) {
			ws_px(c, x + i, y0 + graph_y(0, mn, sp));
		}
	}
	for (int i = 0; i < n - 1; i++) {
		ws_line(c, x + i * step, y0 + graph_y(h->t[i], mn, sp), x + (i + 1) * step,
			y0 + graph_y(h->t[i + 1], mn, sp));
	}
	for (int i = 0; i < n; i++) {
		if (h->pop[i] >= 50) {
			for (int k = -1; k <= 1; k++) {
				ws_px(c, x + i * step + k, y0 + 10);
			}
		}
	}
}

enum ws_item_state ws_item_state(struct ws_render_ctx *rc, const struct ws_item *it)
{
	struct ws_value v, v2;

	switch (it->type) {
	case WS_IT_TEXT:
	case WS_IT_PICTO:
	case WS_IT_SEP:
		return WS_ITEM_OK;
	case WS_IT_ROTATOR:
		return ws_rotator_pick(rc, it) >= 0 ? WS_ITEM_OK : WS_ITEM_EMPTY;
	case WS_IT_RAIN:
		if (!get(rc, it->var, &v)) {
			return WS_ITEM_UNKNOWN;
		}
		return v.num < 0 ? WS_ITEM_EMPTY : WS_ITEM_OK;
	case WS_IT_RANGE:
	case WS_IT_CLOCK:
		return get(rc, it->var, &v) && get(rc, it->var2, &v2) ? WS_ITEM_OK
								      : WS_ITEM_UNKNOWN;
	case WS_IT_GRAPH:
		return get(rc, it->var, &v) && v.hours && v.hours->n >= 2 ? WS_ITEM_OK
									  : WS_ITEM_UNKNOWN;
	default:
		return get(rc, it->var, &v) ? WS_ITEM_OK : WS_ITEM_UNKNOWN;
	}
}

/* Item to draw for `it`: the first alternative whose condition holds, the
 * item itself, or NULL when its own condition is false. */
static const struct ws_item *resolve(struct ws_render_ctx *rc, const struct ws_item *it)
{
	for (int i = 0; i < it->n_alts; i++) {
		const struct ws_item *alt = &rc->cfg->items[it->alts + i];

		if (alt->has_when &&
		    ws_cond_eval(rc->cfg, &alt->when, rc->vars, &rc->now, NULL, 0)) {
			return alt;
		}
	}
	if (it->has_when && !ws_cond_eval(rc->cfg, &it->when, rc->vars, &rc->now, NULL, 0)) {
		return NULL;
	}
	return it;
}

int ws_rotator_pick(struct ws_render_ctx *rc, const struct ws_item *rot)
{
	int avail[WS_MAX_ROT_ITEMS];
	int n = 0;

	for (int i = 0; i < rot->n_children; i++) {
		const struct ws_item *ch = &rc->cfg->items[rot->children + i];
		const struct ws_item *r = resolve(rc, ch);

		if (!r) {
			continue;
		}
		enum ws_item_state s = ws_item_state(rc, r);

		if (s == WS_ITEM_UNKNOWN || (s == WS_ITEM_EMPTY && (r->flags & WS_F_SKIP))) {
			continue;
		}
		avail[n++] = i;
	}
	if (n == 0) {
		return -1;
	}
	int64_t t = rc->sec_of_day >= 0 ? rc->sec_of_day : rc->now.mono;
	int64_t slot = (t - rot->offset) / rot->period;

	if (t - rot->offset < 0 && (t - rot->offset) % rot->period) {
		slot--;
	}
	int k = (int)(((slot % n) + n) % n);

	return avail[k];
}

static void draw_resolved(struct ws_render_ctx *rc, struct ws_canvas *c, const struct ws_item *it)
{
	struct ws_value v;
	char s[24];

	if (it->type == WS_IT_ROTATOR) {
		int k = ws_rotator_pick(rc, it);

		if (k >= 0) {
			const struct ws_item *ch = &rc->cfg->items[it->children + k];
			struct ws_rect outer = c->clip;
			const struct ws_item *r = resolve(rc, ch);

			c->clip = (struct ws_rect){ch->x, ch->y, ch->w, ch->h};
			/* the child frame never leaves the zone (validated) */
			(void)outer;
			draw_resolved(rc, c, r);
			c->clip = outer;
		}
		return;
	}
	enum ws_item_state st = ws_item_state(rc, it);

	if (st == WS_ITEM_UNKNOWN) {
		dash(c, it);
		return;
	}
	get(rc, it->var, &v);
	switch (it->type) {
	case WS_IT_TEMP:
		if (v.type == WS_VT_STR) {
			put(c, it, font_of(it), v.str, it->y);
		} else {
			draw_temp(c, it, v.num);
		}
		break;
	case WS_IT_ICON:
		ws_blit(c, ws_icon((enum ws_cond)v.num, it->form == WS_FORM_L),
			align_x(it, it->form == WS_FORM_L ? 11 : 5), it->y);
		break;
	case WS_IT_NUMBER:
		draw_number(rc, c, it, &v);
		break;
	case WS_IT_RAIN:
		draw_rain(rc, c, it, v.num);
		break;
	case WS_IT_WIND:
		draw_wind(rc, c, it, v.num);
		break;
	case WS_IT_RANGE: {
		struct ws_value mn;

		get(rc, it->var2, &mn);
		temp_str(s, sizeof(s), UP, v.num);
		put(c, it, &ws_font_small, s, it->y);
		temp_str(s, sizeof(s), DOWN, mn.num);
		put(c, it, &ws_font_small, s, it->y + 6);
		break;
	}
	case WS_IT_PRESSURE:
		draw_pressure(rc, c, it, v.num);
		break;
	case WS_IT_HUMIDITY:
		snprintf(s, sizeof(s), "%d%%", ws_round_deci(v.num));
		put_icon(c, it, ws_font_glyph(&ws_font_small, 0x25CA), &ws_font_small, s, it->y);
		break;
	case WS_IT_CO2:
		draw_co2(c, it, v.num);
		break;
	case WS_IT_GRAPH:
		draw_graph(c, it, v.hours);
		break;
	case WS_IT_CLOCK: {
		struct ws_value m;

		get(rc, it->var2, &m);
		snprintf(s, sizeof(s), "%02d:%02d", (int)(v.num / 10) % 100,
			 (int)(m.num / 10) % 100);
		put(c, it, font_of(it), s, it->y);
		break;
	}
	case WS_IT_TEXT:
		put(c, it, &ws_font_small, ws_cfg_str(rc->cfg, it->text), it->y);
		break;
	case WS_IT_PICTO: {
		const struct ws_bitmap *bm =
			ws_cfg_picto(rc->cfg, ws_cfg_str(rc->cfg, it->text), it->form == WS_FORM_L);

		ws_blit(c, bm, align_x(it, bm ? bm->w : 0), it->y);
		break;
	}
	case WS_IT_SEP:
		for (int y = 0; y < 11; y++) {
			if (!(it->flags & WS_F_DASHED) || (y % 2) == 0) {
				ws_px(c, it->x, it->y + y);
			}
		}
		break;
	default:
		break;
	}
}

void ws_render_item(struct ws_render_ctx *rc, const struct ws_item *it, struct ws_frame *f)
{
	struct ws_canvas c;
	const struct ws_item *r = resolve(rc, it);

	if (!r) {
		return;
	}
	ws_canvas_init(&c, f);
	c.clip = (struct ws_rect){it->x, it->y, it->w, it->h};
	draw_resolved(rc, &c, r);
}

void ws_render_screen(struct ws_render_ctx *rc, int screen, struct ws_frame *out)
{
	ws_frame_clear(out);
	if (screen < 0 || screen >= rc->cfg->n_screens) {
		return;
	}
	const struct ws_screen *sc = &rc->cfg->screens[screen];

	for (int i = 0; i < sc->n_items; i++) {
		ws_render_item(rc, &rc->cfg->items[sc->items + i], out);
	}
}
