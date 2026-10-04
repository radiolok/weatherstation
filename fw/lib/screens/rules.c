/* Conditions with hysteresis and the screen selection algorithm (spec, 6). */
#include <stdio.h>
#include <string.h>

#include <ws/screens.h>

static void fmt_val(int var, int32_t v, char *b, size_t n)
{
	struct ws_value x = {true, ws_var_info(var)->type, 0, v, NULL, NULL};

	if (x.type == WS_VT_ANY) {
		x.type = WS_VT_NUM;
	}
	ws_value_format(&x, x.type, b, n);
}

static void describe(const struct ws_cmp *c, const struct ws_value *x, char *why, size_t n)
{
	char cur[24], a[16], b[16];

	if (!why || !n) {
		return;
	}
	ws_value_format(x, x->type, cur, sizeof(cur));
	fmt_val(c->var, c->v[0], a, sizeof(a));
	if (c->op == WS_OP_BETWEEN) {
		fmt_val(c->var, c->v[1], b, sizeof(b));
		snprintf(why, n, "%s=%s %s..%s", ws_var_name(c->var), cur, a, b);
	} else if (c->op == WS_OP_IN) {
		snprintf(why, n, "%s=%s in [%s%s]", ws_var_name(c->var), cur, a,
			 c->n > 1 ? ",…" : "");
	} else {
		snprintf(why, n, "%s=%s %s %s", ws_var_name(c->var), cur, ws_op_name(c->op), a);
	}
}

static bool cmp_eval(struct ws_cmp *c, const struct ws_vars *vars, const struct ws_now *now,
		     char *why, size_t why_len)
{
	struct ws_value x;
	bool r = false;

	if (!ws_vars_get(vars, c->var, now, &x) || x.type == WS_VT_STR || x.type == WS_VT_HOURS) {
		c->state = 0;
		return false;
	}
	int32_t v = x.num, h = c->hyst;
	bool on = c->state;

	switch (c->op) {
	case WS_OP_EQ:
		r = v == c->v[0];
		break;
	case WS_OP_NE:
		r = v != c->v[0];
		break;
	case WS_OP_GT:
		r = on ? v > c->v[0] - h : v > c->v[0];
		break;
	case WS_OP_GE:
		r = on ? v >= c->v[0] - h : v >= c->v[0];
		break;
	case WS_OP_LT:
		r = on ? v < c->v[0] + h : v < c->v[0];
		break;
	case WS_OP_LE:
		r = on ? v <= c->v[0] + h : v <= c->v[0];
		break;
	case WS_OP_BETWEEN:
		r = on ? (v >= c->v[0] - h && v <= c->v[1] + h) : (v >= c->v[0] && v <= c->v[1]);
		break;
	case WS_OP_IN:
		for (int i = 0; i < c->n; i++) {
			r = r || v == c->v[i];
		}
		break;
	default:
		break;
	}
	c->state = r;
	if (r) {
		describe(c, &x, why, why_len);
	}
	return r;
}

bool ws_cond_eval(struct ws_config *cfg, const struct ws_condition *cd, const struct ws_vars *vars,
		  const struct ws_now *now, char *why, size_t why_len)
{
	bool all = true, any = false;
	char tmp[64];

	if (why && why_len) {
		why[0] = '\0';
	}
	/* every comparison is evaluated so that hysteresis state stays current */
	for (int i = 0; i < cd->n; i++) {
		struct ws_cmp *c = &cfg->cmps[cd->first + i];
		bool r;

		tmp[0] = '\0';
		if (c->op == WS_OP_GROUP) {
			struct ws_condition sub = {(uint16_t)c->v[0], (uint8_t)c->v[1],
						   (uint8_t)c->v[2]};

			r = ws_cond_eval(cfg, &sub, vars, now, tmp, sizeof(tmp));
			c->state = r;
		} else {
			r = cmp_eval(c, vars, now, tmp, sizeof(tmp));
		}
		if (r && why && why_len && !why[0]) {
			snprintf(why, why_len, "%s", tmp);
		}
		all = all && r;
		any = any || r;
	}
	bool res = cd->any ? any : all;

	if (!res && why && why_len) {
		why[0] = '\0';
	}
	return res;
}

bool ws_rule_time_ok(const struct ws_rule *r, int minute_of_day, int dow)
{
	if (!r->has_time) {
		return true;
	}
	if (minute_of_day < 0 || dow < 1 || dow > 7) {
		return false; /* time not known yet */
	}
	if (!(r->days & (1u << (dow - 1)))) {
		return false;
	}
	if (r->from_min == r->to_min) {
		return true;
	}
	if (r->from_min < r->to_min) {
		return minute_of_day >= r->from_min && minute_of_day < r->to_min;
	}
	return minute_of_day >= r->from_min || minute_of_day < r->to_min;
}

static void clock_of(const struct ws_vars *vars, const struct ws_now *now, int *minute, int *dow)
{
	struct ws_value h, m, d;

	*minute = -1;
	*dow = 0;
	if (ws_vars_get(vars, WS_V_TIME_HOUR, now, &h) &&
	    ws_vars_get(vars, WS_V_TIME_MIN, now, &m)) {
		*minute = (h.num / 10) * 60 + m.num / 10;
	}
	if (ws_vars_get(vars, WS_V_TIME_DOW, now, &d)) {
		*dow = d.num / 10;
	}
}

static int prio_of(const struct ws_engine *e, int s)
{
	const struct ws_screen *sc = &e->cfg->screens[s];

	return (s == e->cfg->def || !sc->rule.present) ? 0 : sc->rule.prio;
}

void ws_engine_init(struct ws_engine *e, struct ws_config *cfg, int64_t mono)
{
	memset(e, 0, sizeof(*e));
	e->cfg = cfg;
	e->current = cfg->def;
	e->current_since = mono;
	e->pinned = -1;
	snprintf(e->reason, sizeof(e->reason), "по умолчанию");
}

void ws_engine_reconfigure(struct ws_engine *e, struct ws_config *cfg, int64_t mono)
{
	char id[WS_ID_LEN];
	int pinned = -1;

	snprintf(id, sizeof(id), "%s", e->cfg->screens[e->current].id);
	if (e->pinned >= 0) {
		pinned = ws_cfg_screen_by_id(cfg, e->cfg->screens[e->pinned].id);
	}
	int64_t pin_until = e->pin_until;
	bool dbg = e->pin_debug;

	ws_engine_init(e, cfg, mono);
	int cur = ws_cfg_screen_by_id(cfg, id);

	if (cur >= 0) {
		e->current = cur;
	}
	if (pinned >= 0) {
		e->pinned = pinned;
		e->pin_until = pin_until;
		e->pin_debug = dbg;
	}
}

void ws_engine_pin(struct ws_engine *e, int screen, uint32_t minutes, int64_t mono)
{
	if (screen < 0 || screen >= e->cfg->n_screens) {
		if (e->pinned >= 0) {
			e->pinned = -1;
			e->pin_debug = false;
			/* back to the rules right away */
			e->current_since = mono - 86400;
		}
		return;
	}
	e->pinned = screen;
	e->pin_until = minutes ? mono + (int64_t)minutes * 60 : 0;
	e->pin_debug = false;
}

void ws_engine_debug_next(struct ws_engine *e, int64_t mono)
{
	int from = e->pinned >= 0 ? e->pinned : e->current;

	e->pinned = (from + 1) % e->cfg->n_screens;
	e->pin_until = mono + WS_DEBUG_PIN_S;
	e->pin_debug = true;
}

static bool switch_to(struct ws_engine *e, int s, int64_t mono)
{
	if (s == e->current) {
		return false;
	}
	e->current = s;
	e->current_since = mono;
	return true;
}

bool ws_engine_step(struct ws_engine *e, const struct ws_vars *vars, const struct ws_now *now)
{
	struct ws_config *cfg = e->cfg;
	int64_t t = now->mono;
	int minute, dow;
	int winner = cfg->def, wprio = 0;

	clock_of(vars, now, &minute, &dow);
	e->last_candidates = 0;
	for (int s = 0; s < cfg->n_screens; s++) {
		struct ws_screen *sc = &cfg->screens[s];
		struct ws_screen_state *st = &e->st[s];
		const struct ws_rule *r = &sc->rule;

		if (!r->present || s == cfg->def) {
			st->raw = st->active = false;
			continue;
		}
		char why[64];
		bool cond =
			r->cond.n ? ws_cond_eval(cfg, &r->cond, vars, now, why, sizeof(why)) : true;
		bool raw = sc->enabled && ws_rule_time_ok(r, minute, dow) && cond;

		if (!r->cond.n) {
			snprintf(why, sizeof(why), "время %02u:%02u–%02u:%02u", r->from_min / 60,
				 r->from_min % 60, r->to_min / 60, r->to_min % 60);
		}
		if (raw && !st->raw) {
			st->raw_since = t;
		}
		if (!raw && st->raw) {
			st->false_since = t;
		}
		st->raw = raw;
		bool was = st->active;

		if (raw) {
			st->active = was || t - st->raw_since >= (int64_t)r->on_delay;
		} else {
			st->active = was && t - st->false_since < (int64_t)r->off_delay;
		}
		if (st->active && !was) {
			st->active_since = t;
		}
		if (raw) {
			snprintf(st->why, sizeof(st->why), "%s", why);
		}
		bool cand = st->active;

		if (cand && r->mode == WS_MODE_INSERT) {
			int64_t cyc = (int64_t)r->insert_every * 60;

			cand = ((t - st->active_since) % cyc) < r->insert_show;
		}
		if (cand) {
			e->last_candidates |= 1 << s;
			if (r->prio > wprio) {
				winner = s;
				wprio = r->prio;
			}
		}
	}

	/* manual pin wins over every rule */
	if (e->pinned >= 0 && e->pin_until && t >= e->pin_until) {
		e->pinned = -1;
		e->pin_debug = false;
		e->current_since = t - 86400; /* return to the rules at once */
	}
	if (e->pinned >= 0) {
		bool ch = switch_to(e, e->pinned, t);

		snprintf(e->reason, sizeof(e->reason), "%s",
			 e->pin_debug ? "кнопка" : "закреплён вручную");
		return ch;
	}

	int cur = e->current;
	bool cur_cand = (e->last_candidates >> cur) & 1;
	bool changed = false;

	if (winner == cur) {
		/* nothing to do */
	} else if (wprio > prio_of(e, cur) && winner != cfg->def) {
		changed = switch_to(e, winner, t); /* more important: at once */
	} else if (!cur_cand || cur == cfg->def) {
		const struct ws_rule *r = &cfg->screens[cur].rule;
		uint32_t min_show = (r->present && r->mode == WS_MODE_WHILE) ? r->min_show : 0;

		if (cur == cfg->def || t - e->current_since >= (int64_t)min_show) {
			changed = switch_to(e, winner, t);
		}
	}
	if (e->current == cfg->def) {
		snprintf(e->reason, sizeof(e->reason), "по умолчанию");
	} else if ((e->last_candidates >> e->current) & 1) {
		snprintf(e->reason, sizeof(e->reason), "%s", e->st[e->current].why);
	} else {
		snprintf(e->reason, sizeof(e->reason), "минимальное время показа");
	}
	return changed;
}
