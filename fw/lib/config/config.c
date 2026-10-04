/* Screen configuration compiler and serializer, see ws/config.h. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ws/config.h>
#include <ws/vars.h>

static const char *const type_names[WS_IT_COUNT] = {
	"temp", "icon",  "number", "rain", "wind",  "range", "pressure", "humidity",
	"co2",  "graph", "clock",  "text", "picto", "sep",   "rotator",
};

static const char *const form_names[] = {"L", "S", "S2"};
static const char *const op_names[] = {"=", "!=", "<", "<=", ">", ">=", "between", "in", "group"};

const char *ws_item_type_name(int type)
{
	return type >= 0 && type < WS_IT_COUNT ? type_names[type] : "";
}

const char *ws_form_name(int form)
{
	return form >= 0 && form <= WS_FORM_S2 ? form_names[form] : "";
}

const char *ws_op_name(int op)
{
	return op >= 0 && op <= WS_OP_GROUP ? op_names[op] : "";
}

int ws_form_height(int form)
{
	return form == WS_FORM_S ? 5 : 11;
}

const char *ws_cfg_str(const struct ws_config *cfg, uint16_t off)
{
	if (off == WS_NO_IDX || off >= cfg->n_str) {
		return "";
	}
	return &cfg->str[off];
}

int ws_cfg_screen_by_id(const struct ws_config *cfg, const char *id)
{
	for (int i = 0; i < cfg->n_screens; i++) {
		if (strcmp(cfg->screens[i].id, id) == 0) {
			return i;
		}
	}
	return -1;
}

const struct ws_bitmap *ws_cfg_picto(const struct ws_config *cfg, const char *name, bool large)
{
	if (cfg) {
		for (int i = 0; i < cfg->n_pictos; i++) {
			if (strcmp(cfg->pictos[i].name, name) == 0 &&
			    (cfg->pictos[i].bm.w == 11) == large) {
				return &cfg->pictos[i].bm;
			}
		}
	}
	return ws_picto_builtin(name, large);
}

void ws_cfg_reset_state(struct ws_config *cfg)
{
	for (int i = 0; i < cfg->n_cmps; i++) {
		cfg->cmps[i].state = 0;
	}
}

/* ---- default widths (must match fw/web/src/render.js) ---- */

static int sw(const char *s)
{
	return ws_text_width(&ws_font_small, s, 1);
}

static int bw(const char *s)
{
	return ws_text_width(&ws_font_big, s, 1);
}

static int max2(int a, int b)
{
	return a > b ? a : b;
}

int ws_item_default_w(const struct ws_config *cfg, const struct ws_item *it)
{
	bool large = it->form == WS_FORM_L;
	char s[64];

	switch (it->type) {
	case WS_IT_TEMP:
		snprintf(s, sizeof(s), "%s%s", (it->flags & WS_F_TENTHS) ? "-35.5" : "-35",
			 (it->flags & WS_F_DEG) ? "\xC2\xB0" : "");
		return large ? bw(s) : sw(s);
	case WS_IT_ICON:
	case WS_IT_PICTO:
		return large ? 11 : 5;
	case WS_IT_NUMBER: {
		size_t o = (size_t)snprintf(s, sizeof(s), "%s-", ws_cfg_str(cfg, it->prefix));

		for (int i = 0; i < it->digits && o < sizeof(s) - 1; i++) {
			s[o++] = '8';
		}
		if (it->decimals && o < sizeof(s) - 1) {
			s[o++] = '.';
			for (int i = 0; i < it->decimals && o < sizeof(s) - 1; i++) {
				s[o++] = '8';
			}
		}
		s[o] = '\0';
		strncat(s, ws_cfg_str(cfg, it->suffix), sizeof(s) - strlen(s) - 1);
		int w = large ? bw(s) : sw(s);

		if (it->picto != WS_NO_IDX) {
			w += (large ? 11 : 5) + 2;
		}
		return w;
	}
	case WS_IT_RAIN:
		if (large) {
			return 11 + 2 + bw("88-88");
		}
		snprintf(s, sizeof(s), "%s", ws_cfg_str(cfg, it->text));
		if (it->form == WS_FORM_S) {
			return max2(7 + sw("88\xD0\xA7"), 7 + sw(s));
		}
		return max2(max2(7 + sw("88\xD0\xA7"), 7 + sw(s)),
			    sw("\xD0\x94\xD0\x9E 88\xD0\xA7"));
	case WS_IT_WIND:
		return it->form == WS_FORM_S ? 7 + sw("88")
					     : max2(7 + sw("88"), sw("\xD0\x9C/\xD0\xA1"));
	case WS_IT_RANGE:
		return sw("\xE2\x86\x91-35");
	case WS_IT_PRESSURE:
		return it->form == WS_FORM_S ? 7 + sw("888")
					     : max2(7 + sw("888"), sw("\xD0\x9C\xD0\x9C"));
	case WS_IT_HUMIDITY:
		return 7 + sw("100%");
	case WS_IT_CO2:
		return large ? bw("8888") : sw("8888") + 2;
	case WS_IT_GRAPH:
		return 30;
	case WS_IT_CLOCK:
		return large ? bw("88:88") : sw("88:88");
	case WS_IT_TEXT:
		return max2(1, sw(ws_cfg_str(cfg, it->text)));
	case WS_IT_SEP:
		return 1;
	case WS_IT_ROTATOR: {
		int w = 1;

		for (int i = 0; i < it->n_children; i++) {
			w = max2(w, ws_item_default_w(cfg, &cfg->items[it->children + i]));
		}
		return w;
	}
	default:
		return 1;
	}
}

/* ---- compiler ---- */

struct ctx {
	const struct ws_json *j;
	struct ws_config *c;
	struct ws_cfg_errors *e;
};

static void err(struct ctx *x, const char *path, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

static void err(struct ctx *x, const char *path, const char *fmt, ...)
{
	if (x->e->count >= WS_MAX_ERRORS) {
		x->e->count++; /* count, but keep the first ones */
		return;
	}
	struct ws_cfg_error *e = &x->e->e[x->e->count++];
	va_list ap;

	snprintf(e->path, sizeof(e->path), "%s", path);
	va_start(ap, fmt);
	vsnprintf(e->msg, sizeof(e->msg), fmt, ap);
	va_end(ap);
}

static uint16_t add_str(struct ctx *x, const char *s, const char *path)
{
	if (!s || !s[0]) {
		return 0; /* offset 0 is always "" */
	}
	size_t l = strlen(s) + 1;

	if (x->c->n_str + l > WS_POOL_STRINGS) {
		err(x, path, "не хватает памяти под строки");
		return 0;
	}
	uint16_t off = x->c->n_str;

	memcpy(&x->c->str[off], s, l);
	x->c->n_str += (uint16_t)l;
	return off;
}

static int reserve_items(struct ctx *x, int n, const char *path)
{
	if (x->c->n_items + n > WS_POOL_ITEMS) {
		err(x, path, "слишком много элементов во всей конфигурации (не больше %d)",
		    WS_POOL_ITEMS);
		return -1;
	}
	int first = x->c->n_items;

	memset(&x->c->items[first], 0, sizeof(struct ws_item) * n);
	x->c->n_items += (uint16_t)n;
	return first;
}

static int reserve_cmps(struct ctx *x, int n, const char *path)
{
	if (x->c->n_cmps + n > WS_POOL_CMPS) {
		err(x, path, "слишком много сравнений во всей конфигурации (не больше %d)",
		    WS_POOL_CMPS);
		return -1;
	}
	int first = x->c->n_cmps;

	memset(&x->c->cmps[first], 0, sizeof(struct ws_cmp) * n);
	x->c->n_cmps += (uint16_t)n;
	return first;
}

/* Unknown keys are errors: a typo must not silently change the screen. */
static void check_keys(struct ctx *x, int obj, const char *path, const char *const *allowed)
{
	for (int k = ws_json_first(x->j, obj); k >= 0; k = ws_json_next(x->j, obj, k)) {
		bool ok = false;

		for (const char *const *a = allowed; *a; a++) {
			if (ws_json_key_eq(x->j, k, *a)) {
				ok = true;
				break;
			}
		}
		if (!ok) {
			char key[32];

			if (ws_json_str(x->j, k, key, sizeof(key)) < 0) {
				strcpy(key, "?");
			}
			err(x, path, "неизвестное поле «%s»", key);
		}
	}
}

static bool get_int(struct ctx *x, int obj, const char *key, int32_t def, int32_t lo, int32_t hi,
		    int32_t *out, const char *path)
{
	int t = ws_json_get(x->j, obj, key);

	*out = def;
	if (t < 0) {
		return true;
	}
	if (!ws_json_int(x->j, t, out)) {
		err(x, path, "«%s» должно быть целым числом", key);
		return false;
	}
	if (*out < lo || *out > hi) {
		err(x, path, "«%s» = %ld, допустимо %ld…%ld", key, (long)*out, (long)lo, (long)hi);
		*out = def;
		return false;
	}
	return true;
}

static bool get_bool(struct ctx *x, int obj, const char *key, bool def, const char *path)
{
	int t = ws_json_get(x->j, obj, key);
	bool b = def;

	if (t >= 0 && !ws_json_bool(x->j, t, &b)) {
		err(x, path, "«%s» должно быть true или false", key);
	}
	return b;
}

static bool get_str(struct ctx *x, int obj, const char *key, char *buf, size_t len,
		    const char *path)
{
	int t = ws_json_get(x->j, obj, key);

	buf[0] = '\0';
	if (t < 0) {
		return false;
	}
	if (ws_json_str(x->j, t, buf, len) < 0) {
		err(x, path, "«%s» должно быть строкой не длиннее %u байт", key,
		    (unsigned int)len - 1);
		buf[0] = '\0';
		return false;
	}
	return true;
}

static bool valid_id(const char *s, bool lower_only)
{
	if (!s[0]) {
		return false;
	}
	for (; *s; s++) {
		char c = *s;

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
		      (!lower_only && c >= 'A' && c <= 'Z'))) {
			return false;
		}
	}
	return true;
}

static int parse_value(struct ctx *x, int tok, int var, int32_t *out, const char *path)
{
	const struct ws_var_info *inf = ws_var_info(var);
	char s[16];
	bool b;

	switch (inf->type) {
	case WS_VT_BOOL:
		if (ws_json_bool(x->j, tok, &b)) {
			*out = b;
			return 0;
		}
		err(x, path, "%s: ожидается true или false", inf->name);
		return -1;
	case WS_VT_COND:
	case WS_VT_DIR: {
		int e = -1;

		if (ws_json_str(x->j, tok, s, sizeof(s)) > 0) {
			e = inf->type == WS_VT_COND ? ws_cond_parse(s) : ws_dir_parse(s);
		}
		if (e < 0) {
			err(x, path, "%s: неизвестное значение", inf->name);
			return -1;
		}
		*out = e;
		return 0;
	}
	case WS_VT_NUM:
	case WS_VT_ANY:
		if (ws_json_deci(x->j, tok, out)) {
			return 0;
		}
		err(x, path, "%s: ожидается число", inf->name);
		return -1;
	default:
		err(x, path, "%s: с этой переменной нельзя сравнивать", inf->name);
		return -1;
	}
}

static int parse_op(const char *s)
{
	static const struct {
		const char *s;
		int op;
	} ops[] = {
		{"=", WS_OP_EQ},
		{"==", WS_OP_EQ},
		{"!=", WS_OP_NE},
		{"\xE2\x89\xA0", WS_OP_NE},
		{"<", WS_OP_LT},
		{"<=", WS_OP_LE},
		{"\xE2\x89\xA4", WS_OP_LE},
		{">", WS_OP_GT},
		{">=", WS_OP_GE},
		{"\xE2\x89\xA5", WS_OP_GE},
		{"between", WS_OP_BETWEEN},
		{"in", WS_OP_IN},
	};

	for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
		if (strcmp(ops[i].s, s) == 0) {
			return ops[i].op;
		}
	}
	return -1;
}

static int parse_cond(struct ctx *x, int obj, struct ws_condition *out, int max_leaves,
		      bool allow_nest, const char *path, int *leaves);

static void parse_cmp(struct ctx *x, int tok, struct ws_cmp *c, const char *path)
{
	static const char *const keys[] = {"var", "op", "val", "hyst", NULL};
	char name[24], op[12];

	check_keys(x, tok, path, keys);
	get_str(x, tok, "var", name, sizeof(name), path);
	int var = ws_var_lookup(name);

	if (var < 0) {
		err(x, path, "неизвестная переменная «%s»", name);
		return;
	}
	c->var = (uint8_t)var;
	get_str(x, tok, "op", op, sizeof(op), path);
	int o = parse_op(op);

	if (o < 0) {
		err(x, path, "неизвестная операция «%s»", op);
		return;
	}
	c->op = (uint8_t)o;
	int vt = ws_json_get(x->j, tok, "val");

	if (vt < 0) {
		err(x, path, "нет значения «val»");
		return;
	}
	if (o == WS_OP_BETWEEN || o == WS_OP_IN) {
		int n = 0;

		if (!ws_json_is(x->j, vt, WS_J_ARR)) {
			err(x, path, "«val» должно быть списком");
			return;
		}
		for (int k = ws_json_first(x->j, vt); k >= 0; k = ws_json_next(x->j, vt, k)) {
			if (n >= WS_MAX_LIST) {
				err(x, path, "в списке не больше %d значений", WS_MAX_LIST);
				return;
			}
			if (parse_value(x, k, var, &c->v[n], path) < 0) {
				return;
			}
			n++;
		}
		if (o == WS_OP_BETWEEN && (n != 2 || c->v[0] > c->v[1])) {
			err(x, path, "для «between» нужно [от, до], от ≤ до");
			return;
		}
		if (o == WS_OP_IN && n == 0) {
			err(x, path, "пустой список «in»");
			return;
		}
		c->n = (uint8_t)n;
	} else {
		if (parse_value(x, vt, var, &c->v[0], path) < 0) {
			return;
		}
		c->n = 1;
	}
	int ht = ws_json_get(x->j, tok, "hyst");

	if (ht >= 0) {
		if (!ws_json_deci(x->j, ht, &c->hyst) || c->hyst < 0) {
			err(x, path, "гистерезис должен быть числом ≥ 0");
		}
	}
}

/* {"all": [...]} or {"any": [...]} */
static int parse_cond(struct ctx *x, int obj, struct ws_condition *out, int max_leaves,
		      bool allow_nest, const char *path, int *leaves)
{
	int all = ws_json_get(x->j, obj, "all");
	int any = ws_json_get(x->j, obj, "any");
	int list = all >= 0 ? all : any;
	char p[128];

	memset(out, 0, sizeof(*out));
	if (all >= 0 && any >= 0) {
		err(x, path, "нужно что-то одно: «all» или «any»");
		return -1;
	}
	if (list < 0) {
		return 0; /* no condition */
	}
	if (!ws_json_is(x->j, list, WS_J_ARR)) {
		err(x, path, "«%s» должно быть списком сравнений", all >= 0 ? "all" : "any");
		return -1;
	}
	int n = x->j->t[list].size;

	if (n == 0) {
		err(x, path, "пустой список сравнений");
		return -1;
	}
	int first = reserve_cmps(x, n, path);

	if (first < 0) {
		return -1;
	}
	out->first = (uint16_t)first;
	out->n = (uint8_t)n;
	out->any = any >= 0;
	int i = 0;

	for (int k = ws_json_first(x->j, list); k >= 0; k = ws_json_next(x->j, list, k), i++) {
		struct ws_cmp *c = &x->c->cmps[first + i];

		snprintf(p, sizeof(p), "%s.%s[%d]", path, all >= 0 ? "all" : "any", i);
		if (!ws_json_is(x->j, k, WS_J_OBJ)) {
			err(x, p, "ожидается объект сравнения");
			continue;
		}
		if (ws_json_get(x->j, k, "all") >= 0 || ws_json_get(x->j, k, "any") >= 0) {
			if (!allow_nest) {
				err(x, p, "допускается только один уровень вложенности");
				continue;
			}
			struct ws_condition sub;

			if (parse_cond(x, k, &sub, max_leaves, false, p, leaves) == 0) {
				c = &x->c->cmps[first + i];
				c->op = WS_OP_GROUP;
				c->v[0] = sub.first;
				c->v[1] = sub.n;
				c->v[2] = sub.any;
			}
			continue;
		}
		parse_cmp(x, k, c, p);
		(*leaves)++;
	}
	if (*leaves > max_leaves) {
		err(x, path, "сравнений больше %d", max_leaves);
		return -1;
	}
	return 0;
}

static int parse_time(struct ctx *x, int tok, uint16_t *out, const char *key, const char *path)
{
	char s[8];
	unsigned int h, m;

	if (!get_str(x, tok, key, s, sizeof(s), path) || strlen(s) != 5 || s[2] != ':' ||
	    sscanf(s, "%2u:%2u", &h, &m) != 2 || h > 23 || m > 59) {
		err(x, path, "«%s»: время в виде ЧЧ:ММ", key);
		return -1;
	}
	*out = (uint16_t)(h * 60 + m);
	return 0;
}

static void parse_rule(struct ctx *x, int tok, struct ws_rule *r, const char *path)
{
	static const char *const keys[] = {"all",       "any",      "time", "prio",   "on_delay",
					   "off_delay", "min_show", "mode", "insert", NULL};
	int leaves = 0;
	int32_t v;

	memset(r, 0, sizeof(*r));
	if (!ws_json_is(x->j, tok, WS_J_OBJ)) {
		err(x, path, "правило должно быть объектом");
		return;
	}
	check_keys(x, tok, path, keys);
	r->present = true;
	parse_cond(x, tok, &r->cond, WS_MAX_RULE_CMPS, true, path, &leaves);

	int t = ws_json_get(x->j, tok, "time");

	r->days = 0x7F;
	if (t >= 0) {
		static const char *const tkeys[] = {"from", "to", "days", NULL};
		char p[128];

		snprintf(p, sizeof(p), "%s.time", path);
		check_keys(x, t, p, tkeys);
		r->has_time = true;
		parse_time(x, t, &r->from_min, "from", p);
		parse_time(x, t, &r->to_min, "to", p);
		int d = ws_json_get(x->j, t, "days");

		if (d >= 0) {
			r->days = 0;
			if (!ws_json_is(x->j, d, WS_J_ARR)) {
				err(x, p, "«days» — список дней 1…7");
			}
			for (int k = ws_json_first(x->j, d); k >= 0; k = ws_json_next(x->j, d, k)) {
				int32_t day;

				if (!ws_json_int(x->j, k, &day) || day < 1 || day > 7) {
					err(x, p, "«days» — список дней 1…7");
					break;
				}
				r->days |= (uint8_t)(1u << (day - 1));
			}
			if (!r->days) {
				err(x, p, "не выбран ни один день");
			}
		}
	}
	if (r->cond.n == 0 && !r->has_time) {
		err(x, path, "у правила нет ни условия, ни окна времени");
	}
	get_int(x, tok, "prio", 50, 1, 99, &v, path);
	r->prio = (uint8_t)v;
	get_int(x, tok, "on_delay", 0, 0, 86400, &v, path);
	r->on_delay = (uint32_t)v;
	get_int(x, tok, "off_delay", 0, 0, 86400, &v, path);
	r->off_delay = (uint32_t)v;
	get_int(x, tok, "min_show", 300, 0, 86400, &v, path);
	r->min_show = (uint32_t)v;

	char mode[12];

	r->mode = WS_MODE_WHILE;
	if (get_str(x, tok, "mode", mode, sizeof(mode), path)) {
		if (strcmp(mode, "insert") == 0) {
			r->mode = WS_MODE_INSERT;
		} else if (strcmp(mode, "while") != 0) {
			err(x, path, "«mode»: while или insert");
		}
	}
	if (r->mode == WS_MODE_INSERT) {
		int ins = ws_json_get(x->j, tok, "insert");
		char p[128];

		snprintf(p, sizeof(p), "%s.insert", path);
		if (!ws_json_is(x->j, ins, WS_J_OBJ)) {
			err(x, path, "для режима insert нужно «insert»: {show, every}");
			return;
		}
		static const char *const ikeys[] = {"show", "every", NULL};

		check_keys(x, ins, p, ikeys);
		get_int(x, ins, "show", 30, 5, 3600, &v, p);
		r->insert_show = (uint16_t)v;
		get_int(x, ins, "every", 10, 1, 1440, &v, p);
		r->insert_every = (uint16_t)v;
		if (r->insert_show >= r->insert_every * 60) {
			err(x, p, "«show» должно быть меньше периода «every»");
		}
	}
}

/* Allowed forms and the default one (first) for each type. */
static const uint8_t forms_allowed[WS_IT_COUNT] = {
	[WS_IT_TEMP] = 1 << WS_FORM_L | 1 << WS_FORM_S,
	[WS_IT_ICON] = 1 << WS_FORM_L | 1 << WS_FORM_S,
	[WS_IT_NUMBER] = 1 << WS_FORM_L | 1 << WS_FORM_S,
	[WS_IT_RAIN] = 1 << WS_FORM_L | 1 << WS_FORM_S | 1 << WS_FORM_S2,
	[WS_IT_WIND] = 1 << WS_FORM_S | 1 << WS_FORM_S2,
	[WS_IT_RANGE] = 1 << WS_FORM_S2,
	[WS_IT_PRESSURE] = 1 << WS_FORM_S | 1 << WS_FORM_S2,
	[WS_IT_HUMIDITY] = 1 << WS_FORM_S,
	[WS_IT_CO2] = 1 << WS_FORM_L | 1 << WS_FORM_S2,
	[WS_IT_GRAPH] = 1 << WS_FORM_L,
	[WS_IT_CLOCK] = 1 << WS_FORM_L | 1 << WS_FORM_S,
	[WS_IT_TEXT] = 1 << WS_FORM_S,
	[WS_IT_PICTO] = 1 << WS_FORM_L | 1 << WS_FORM_S,
	[WS_IT_SEP] = 1 << WS_FORM_L,
	[WS_IT_ROTATOR] = 1 << WS_FORM_L,
};

static const uint8_t form_default[WS_IT_COUNT] = {
	[WS_IT_TEMP] = WS_FORM_L,      [WS_IT_ICON] = WS_FORM_L,     [WS_IT_NUMBER] = WS_FORM_L,
	[WS_IT_RAIN] = WS_FORM_S2,     [WS_IT_WIND] = WS_FORM_S2,    [WS_IT_RANGE] = WS_FORM_S2,
	[WS_IT_PRESSURE] = WS_FORM_S2, [WS_IT_HUMIDITY] = WS_FORM_S, [WS_IT_CO2] = WS_FORM_L,
	[WS_IT_GRAPH] = WS_FORM_L,     [WS_IT_CLOCK] = WS_FORM_L,    [WS_IT_TEXT] = WS_FORM_S,
	[WS_IT_PICTO] = WS_FORM_L,     [WS_IT_SEP] = WS_FORM_L,      [WS_IT_ROTATOR] = WS_FORM_L,
};

static const uint8_t var_default[WS_IT_COUNT] = {
	[WS_IT_TEMP] = WS_V_OUT_T,      [WS_IT_ICON] = WS_V_OUT_COND,
	[WS_IT_NUMBER] = WS_V_NONE,     [WS_IT_RAIN] = WS_V_FC_RAIN_FROM,
	[WS_IT_WIND] = WS_V_OUT_WIND,   [WS_IT_RANGE] = WS_V_FC_TMAX,
	[WS_IT_PRESSURE] = WS_V_IN_P,   [WS_IT_HUMIDITY] = WS_V_IN_RH,
	[WS_IT_CO2] = WS_V_IN_CO2,      [WS_IT_GRAPH] = WS_V_FC_HOURS,
	[WS_IT_CLOCK] = WS_V_TIME_HOUR, [WS_IT_TEXT] = WS_V_NONE,
	[WS_IT_PICTO] = WS_V_NONE,      [WS_IT_SEP] = WS_V_NONE,
	[WS_IT_ROTATOR] = WS_V_NONE,
};

static bool var_fits(int type, int var)
{
	const struct ws_var_info *inf = ws_var_info(var);

	switch (type) {
	case WS_IT_TEMP:
	case WS_IT_HUMIDITY:
	case WS_IT_CO2:
		return inf->type == WS_VT_NUM || inf->type == WS_VT_ANY;
	case WS_IT_NUMBER:
		return inf->type == WS_VT_NUM || inf->type == WS_VT_ANY;
	case WS_IT_ICON:
		return inf->type == WS_VT_COND;
	case WS_IT_WIND:
		return var == WS_V_OUT_WIND || var == WS_V_OBS_WIND;
	case WS_IT_PRESSURE:
		return var == WS_V_IN_P || var == WS_V_OUT_P || var == WS_V_OBS_P;
	default:
		/* elements with a fixed variable */
		return var == var_default[type];
	}
}

static int second_var(int type, int var)
{
	switch (type) {
	case WS_IT_WIND:
		return var == WS_V_OBS_WIND ? WS_V_OBS_WIND_DIR : WS_V_OUT_WIND_DIR;
	case WS_IT_RANGE:
		return WS_V_FC_TMIN;
	case WS_IT_PRESSURE:
		return var == WS_V_IN_P ? WS_V_IN_P_TREND : WS_V_NONE;
	case WS_IT_CLOCK:
		return WS_V_TIME_MIN;
	case WS_IT_RAIN:
		return WS_V_FC_RAIN_TO;
	default:
		return WS_V_NONE;
	}
}

struct frame {
	int x, y, w, h;
};

static bool overlap(const struct ws_item *a, const struct ws_item *b)
{
	return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

enum item_kind {
	KIND_TOP,
	KIND_CHILD,
	KIND_ALT,
};

static void parse_item(struct ctx *x, int tok, int idx, enum item_kind kind,
		       const struct frame *parent, const char *path);

static void parse_alts(struct ctx *x, int tok, int idx, const char *path)
{
	int a = ws_json_get(x->j, tok, "alts");
	char p[128];

	if (a < 0) {
		return;
	}
	if (!ws_json_is(x->j, a, WS_J_ARR)) {
		err(x, path, "«alts» должно быть списком");
		return;
	}
	int n = x->j->t[a].size;

	if (n > WS_MAX_ALTS) {
		err(x, path, "вариантов подмены больше %d", WS_MAX_ALTS);
		return;
	}
	if (n == 0) {
		return;
	}
	int first = reserve_items(x, n, path);

	if (first < 0) {
		return;
	}
	struct ws_item *it = &x->c->items[idx];

	it->alts = (uint16_t)first;
	it->n_alts = (uint8_t)n;
	struct frame f = {it->x, it->y, it->w, it->h};
	int i = 0;

	for (int k = ws_json_first(x->j, a); k >= 0; k = ws_json_next(x->j, a, k), i++) {
		snprintf(p, sizeof(p), "%s.alts[%d]", path, i);
		parse_item(x, k, first + i, KIND_ALT, &f, p);
	}
}

static void parse_children(struct ctx *x, int tok, int idx, bool explicit_w, const char *path)
{
	int a = ws_json_get(x->j, tok, "items");
	char p[128];
	struct ws_item *it = &x->c->items[idx];

	if (!ws_json_is(x->j, a, WS_J_ARR) || x->j->t[a].size == 0) {
		err(x, path, "у зоны ротации нет элементов «items»");
		return;
	}
	int n = x->j->t[a].size;

	if (n > WS_MAX_ROT_ITEMS) {
		err(x, path, "в зоне ротации больше %d элементов", WS_MAX_ROT_ITEMS);
		return;
	}
	int first = reserve_items(x, n, path);

	if (first < 0) {
		return;
	}
	it = &x->c->items[idx];
	it->children = (uint16_t)first;
	it->n_children = (uint8_t)n;
	/* w = -1: the zone has no explicit width yet, children use their own */
	struct frame f = {it->x, it->y, explicit_w ? it->w : -1, it->h};
	int i = 0;

	for (int k = ws_json_first(x->j, a); k >= 0; k = ws_json_next(x->j, a, k), i++) {
		snprintf(p, sizeof(p), "%s.items[%d]", path, i);
		parse_item(x, k, first + i, KIND_CHILD, &f, p);
	}
}

static void parse_item(struct ctx *x, int tok, int idx, enum item_kind kind,
		       const struct frame *parent, const char *path)
{
	static const char *const keys[] = {
		"type",   "form",   "var",   "x",      "y",      "w",      "h",        "align",
		"plus",   "tenths", "deg",   "skip",   "dashed", "dir",    "decimals", "digits",
		"prefix", "suffix", "picto", "invert", "trend",  "hours",  "none",     "text",
		"name",   "when",   "alts",  "items",  "period", "offset", NULL};
	struct ws_item *it = &x->c->items[idx];
	char s[48];
	int32_t v;

	if (!ws_json_is(x->j, tok, WS_J_OBJ)) {
		err(x, path, "элемент должен быть объектом");
		return;
	}
	check_keys(x, tok, path, keys);
	get_str(x, tok, "type", s, sizeof(s), path);
	int type = -1;

	for (int t = 0; t < WS_IT_COUNT; t++) {
		if (strcmp(type_names[t], s) == 0) {
			type = t;
		}
	}
	if (type < 0) {
		err(x, path, "неизвестный тип элемента «%s»", s);
		return;
	}
	if (type == WS_IT_ROTATOR && kind != KIND_TOP) {
		err(x, path, "зона ротации не может быть внутри другого элемента");
		return;
	}
	it->type = (uint8_t)type;
	it->var = WS_V_NONE;
	it->var2 = WS_V_NONE;
	it->picto = WS_NO_IDX;
	it->text = 0;

	/* form */
	it->form = form_default[type];
	if (get_str(x, tok, "form", s, sizeof(s), path)) {
		int f = -1;

		for (int k = 0; k <= WS_FORM_S2; k++) {
			if (strcmp(form_names[k], s) == 0) {
				f = k;
			}
		}
		if (f < 0 || !(forms_allowed[type] & (1 << f))) {
			err(x, path, "у типа «%s» нет формы «%s»", type_names[type], s);
			return;
		}
		it->form = (uint8_t)f;
	}

	/* variable */
	int var = var_default[type];

	if (get_str(x, tok, "var", s, sizeof(s), path)) {
		var = ws_var_lookup(s);
		if (var < 0) {
			err(x, path, "неизвестная переменная «%s»", s);
			return;
		}
		if (!var_fits(type, var)) {
			err(x, path, "переменная «%s» не подходит элементу «%s»", s,
			    type_names[type]);
			return;
		}
	} else if (type == WS_IT_NUMBER) {
		err(x, path, "у элемента «number» нужна переменная «var»");
		return;
	}
	it->var = (uint8_t)var;
	it->var2 = (uint8_t)second_var(type, var);

	/* parameters */
	if (get_bool(x, tok, "plus", false, path)) {
		it->flags |= WS_F_PLUS;
	}
	if (get_bool(x, tok, "tenths", false, path)) {
		it->flags |= WS_F_TENTHS;
	}
	if (get_bool(x, tok, "deg", true, path)) {
		it->flags |= WS_F_DEG;
	}
	if (get_bool(x, tok, "skip", type == WS_IT_RAIN, path)) {
		it->flags |= WS_F_SKIP;
	}
	if (get_bool(x, tok, "dashed", false, path)) {
		it->flags |= WS_F_DASHED;
	}
	if (get_str(x, tok, "dir", s, sizeof(s), path)) {
		if (strcmp(s, "from") == 0) {
			it->flags |= WS_F_DIR_FROM;
		} else if (strcmp(s, "to") != 0) {
			err(x, path, "«dir»: to или from");
		}
	}
	get_int(x, tok, "decimals", 0, 0, 2, &v, path);
	it->decimals = (uint8_t)v;
	get_int(x, tok, "digits", 4, 1, 6, &v, path);
	it->digits = (uint8_t)v;
	if (get_str(x, tok, "prefix", s, 16, path)) {
		it->prefix = add_str(x, s, path);
	}
	if (get_str(x, tok, "suffix", s, 16, path)) {
		it->suffix = add_str(x, s, path);
	}
	if (get_str(x, tok, "picto", s, WS_ID_LEN, path)) {
		if (!ws_cfg_picto(x->c, s, it->form == WS_FORM_L)) {
			err(x, path, "нет пиктограммы «%s» размера %d", s,
			    it->form == WS_FORM_L ? 11 : 5);
		}
		it->picto = add_str(x, s, path);
	}
	if (type == WS_IT_CO2) {
		get_int(x, tok, "invert", it->form == WS_FORM_L ? 0 : 1000, 0, 100000, &v, path);
		it->thr = v * 10;
	}
	if (type == WS_IT_PRESSURE) {
		int t = ws_json_get(x->j, tok, "trend");

		it->thr = 10;
		if (t >= 0 && (!ws_json_deci(x->j, t, &it->thr) || it->thr < 0)) {
			err(x, path, "«trend» — порог тренда в мм, число ≥ 0");
		}
	}
	if (type == WS_IT_GRAPH) {
		get_int(x, tok, "hours", 12, 2, 12, &v, path);
		it->hours = (uint16_t)v;
	} else if (type == WS_IT_RANGE || type == WS_IT_RAIN) {
		get_int(x, tok, "hours", 16, 1, 24, &v, path);
		if (type == WS_IT_RANGE && v != 8 && v != 12 && v != 16) {
			err(x, path, "«hours»: 8, 12 или 16");
		}
		it->hours = (uint16_t)v;
	}
	if (type == WS_IT_RAIN) {
		if (!get_str(x, tok, "none", s, 16, path)) {
			strcpy(s, "\xD0\x9D\xD0\x95\xD0\xA2"); /* НЕТ */
		}
		it->text = add_str(x, s, path);
	}
	if (type == WS_IT_TEXT) {
		char t[64];

		if (!get_str(x, tok, "text", t, sizeof(t), path) || !t[0]) {
			err(x, path, "у элемента «text» нет текста");
		} else {
			/* up to 20 characters */
			int n = 0;

			for (const char *p = t; *p; n++) {
				uint32_t cp;

				p += ws_utf8_next(p, &cp);
			}
			if (n > 20) {
				err(x, path, "текст длиннее 20 символов");
			}
		}
		it->text = add_str(x, t, path);
	}
	if (type == WS_IT_PICTO) {
		if (!get_str(x, tok, "name", s, WS_ID_LEN, path) ||
		    !ws_cfg_picto(x->c, s, it->form == WS_FORM_L)) {
			err(x, path, "нет пиктограммы «%s» размера %d", s,
			    it->form == WS_FORM_L ? 11 : 5);
		}
		it->text = add_str(x, s, path);
	}
	if (get_str(x, tok, "align", s, sizeof(s), path)) {
		if (strcmp(s, "left") == 0) {
			it->align = WS_ALIGN_LEFT;
		} else if (strcmp(s, "center") == 0) {
			it->align = WS_ALIGN_CENTER;
		} else if (strcmp(s, "right") == 0) {
			it->align = WS_ALIGN_RIGHT;
		} else {
			err(x, path, "«align»: left, center или right");
		}
	}

	/* frame */
	if (kind == KIND_ALT) {
		/* the alternative shares the frame of its item */
		static const char *const forbidden[] = {"x", "y", "w", "h", "alts", "items"};

		for (size_t k = 0; k < sizeof(forbidden) / sizeof(forbidden[0]); k++) {
			if (ws_json_get(x->j, tok, forbidden[k]) >= 0) {
				err(x, path, "у варианта подмены нет своей рамки («%s»)",
				    forbidden[k]);
			}
		}
		it->x = (int16_t)parent->x;
		it->y = (int16_t)parent->y;
		it->w = (int16_t)parent->w;
		it->h = (int16_t)parent->h;
		if (ws_form_height(it->form) > parent->h) {
			err(x, path, "вариант выше рамки элемента");
		}
		if (ws_json_get(x->j, tok, "when") < 0) {
			err(x, path, "у варианта подмены нужно условие «when»");
		}
	} else {
		int dx = kind == KIND_CHILD ? parent->x : 0;
		int dy = kind == KIND_CHILD ? parent->y : 0;

		get_int(x, tok, "x", dx, -1000, 1000, &v, path);
		it->x = (int16_t)v;
		get_int(x, tok, "y", dy, -1000, 1000, &v, path);
		it->y = (int16_t)v;
		it->h = (int16_t)ws_form_height(it->form);
		if (type == WS_IT_ROTATOR) {
			get_int(x, tok, "h", 11, 5, 11, &v, path);
			if (v != 5 && v != 11) {
				err(x, path, "высота зоны ротации 5 или 11");
			}
			it->h = (int16_t)v;
			get_int(x, tok, "period", 60, 15, 3600, &v, path);
			it->period = (uint16_t)v;
			get_int(x, tok, "offset", 0, 0, 3599, &v, path);
			if (v >= it->period) {
				err(x, path, "«offset» должен быть меньше периода");
				v = 0;
			}
			it->offset = (uint16_t)v;
		}
		/* allowed rows: L and S2 at y = 0, S at 0, 3 or 6 */
		if (it->h == 11 && it->y != 0) {
			err(x, path, "форма высотой 11 ставится только в y = 0");
		} else if (it->h == 5 && it->y != 0 && it->y != 3 && it->y != 6) {
			err(x, path, "мелкая форма ставится в y = 0, 3 или 6");
		}
	}

	/* when */
	int wt = ws_json_get(x->j, tok, "when");

	if (wt >= 0) {
		char p[128];
		int leaves = 0;

		snprintf(p, sizeof(p), "%s.when", path);
		if (!ws_json_is(x->j, wt, WS_J_OBJ)) {
			err(x, p, "условие должно быть объектом");
		} else {
			static const char *const wkeys[] = {"all", "any", NULL};

			check_keys(x, wt, p, wkeys);
			if (parse_cond(x, wt, &it->when, WS_MAX_WHEN_CMPS, false, p, &leaves) ==
			    0) {
				it->has_when = it->when.n > 0;
			}
		}
		it = &x->c->items[idx];
	}

	if (type == WS_IT_ROTATOR) {
		/* width before children: needed as their default frame */
		get_int(x, tok, "w", -1, 1, WS_W, &v, path);
		bool had_w = v > 0;

		it->w = (int16_t)(had_w ? v : 1);
		if (ws_json_get(x->j, tok, "alts") >= 0) {
			err(x, path, "у зоны ротации нет вариантов подмены");
		}
		parse_children(x, tok, idx, had_w, path);
		it = &x->c->items[idx];
		if (!had_w) {
			it->w = (int16_t)ws_item_default_w(x->c, it);
		}
		/* children must lie inside the zone */
		for (int i = 0; i < it->n_children; i++) {
			struct ws_item *ch = &x->c->items[it->children + i];
			char p[128];

			snprintf(p, sizeof(p), "%s.items[%d]", path, i);
			if (ch->x < it->x || ch->x + ch->w > it->x + it->w || ch->y < it->y ||
			    ch->y + ch->h > it->y + it->h) {
				err(x, p, "элемент выходит за рамку зоны ротации");
			}
		}
	} else if (kind != KIND_ALT) {
		int def = ws_item_default_w(x->c, it);

		if (kind == KIND_CHILD && parent->w > 0) {
			def = parent->w - (it->x - parent->x);
		}
		get_int(x, tok, "w", def, 1, WS_W, &v, path);
		it->w = (int16_t)v;
	}
	if ((it->type == WS_IT_ICON || it->type == WS_IT_PICTO) &&
	    it->w < ws_item_default_w(x->c, it)) {
		err(x, path, "рамка уже элемента (%d < %d)", it->w, ws_item_default_w(x->c, it));
	}
	if (it->x < 0 || it->y < 0 || it->x + it->w > WS_W || it->y + it->h > WS_H) {
		err(x, path, "рамка выходит за холст 102×11");
	}
	if (kind != KIND_ALT && type != WS_IT_ROTATOR) {
		parse_alts(x, tok, idx, path);
	}
}

static void parse_screen(struct ctx *x, int tok, int si, const char *path)
{
	static const char *const keys[] = {"id",   "name",  "default", "enabled",
					   "rule", "items", NULL};
	struct ws_screen *sc = &x->c->screens[si];
	char p[128];

	memset(sc, 0, sizeof(*sc));
	if (!ws_json_is(x->j, tok, WS_J_OBJ)) {
		err(x, path, "экран должен быть объектом");
		return;
	}
	check_keys(x, tok, path, keys);
	if (!get_str(x, tok, "id", sc->id, sizeof(sc->id), path) || !valid_id(sc->id, false)) {
		err(x, path, "«id»: латиница, цифры и _, до %d символов", WS_ID_LEN - 1);
	}
	for (int i = 0; i < si; i++) {
		if (sc->id[0] && strcmp(x->c->screens[i].id, sc->id) == 0) {
			err(x, path, "«id» «%s» уже есть у screens[%d]", sc->id, i);
		}
	}
	if (!get_str(x, tok, "name", sc->name, sizeof(sc->name), path)) {
		strncpy(sc->name, sc->id, sizeof(sc->name) - 1);
	}
	sc->is_default = get_bool(x, tok, "default", false, path);
	sc->enabled = get_bool(x, tok, "enabled", true, path);

	int r = ws_json_get(x->j, tok, "rule");

	if (r >= 0 && !ws_json_is(x->j, r, WS_J_NULL)) {
		snprintf(p, sizeof(p), "%s.rule", path);
		parse_rule(x, r, &sc->rule, p);
	}
	if (sc->is_default && sc->rule.present) {
		err(x, path, "у экрана по умолчанию не бывает правила");
	}
	if (sc->is_default && !sc->enabled) {
		err(x, path, "экран по умолчанию нельзя отключить");
	}

	int items = ws_json_get(x->j, tok, "items");

	if (!ws_json_is(x->j, items, WS_J_ARR)) {
		err(x, path, "нет списка элементов «items»");
		return;
	}
	int n = x->j->t[items].size;

	if (n > WS_MAX_ITEMS) {
		err(x, path, "элементов больше %d", WS_MAX_ITEMS);
		return;
	}
	int first = reserve_items(x, n, path);

	if (first < 0) {
		return;
	}
	sc->items = (uint16_t)first;
	sc->n_items = (uint8_t)n;
	int i = 0, rot = 0;
	struct frame canvas = {0, 0, WS_W, WS_H};

	for (int k = ws_json_first(x->j, items); k >= 0; k = ws_json_next(x->j, items, k), i++) {
		snprintf(p, sizeof(p), "%s.items[%d]", path, i);
		parse_item(x, k, first + i, KIND_TOP, &canvas, p);
		if (x->c->items[first + i].type == WS_IT_ROTATOR) {
			rot++;
		}
	}
	if (rot > WS_MAX_ROTATORS) {
		err(x, path, "зон ротации больше %d", WS_MAX_ROTATORS);
	}
	for (int a = 0; a < n; a++) {
		for (int b = 0; b < a; b++) {
			if (overlap(&x->c->items[first + a], &x->c->items[first + b])) {
				snprintf(p, sizeof(p), "%s.items[%d]", path, a);
				err(x, p, "перекрывает items[%d]", b);
			}
		}
	}
}

static void parse_pictos(struct ctx *x, int arr)
{
	char p[64];
	int i = 0;

	if (arr < 0) {
		return;
	}
	if (!ws_json_is(x->j, arr, WS_J_ARR)) {
		err(x, "pictos", "должно быть списком");
		return;
	}
	if (x->j->t[arr].size > WS_MAX_PICTOS) {
		err(x, "pictos", "своих пиктограмм больше %d", WS_MAX_PICTOS);
		return;
	}
	for (int k = ws_json_first(x->j, arr); k >= 0; k = ws_json_next(x->j, arr, k), i++) {
		static const char *const keys[] = {"name", "size", "rows", NULL};
		struct ws_picto *pc = &x->c->pictos[x->c->n_pictos];
		int32_t size;

		snprintf(p, sizeof(p), "pictos[%d]", i);
		if (!ws_json_is(x->j, k, WS_J_OBJ)) {
			err(x, p, "пиктограмма должна быть объектом");
			continue;
		}
		check_keys(x, k, p, keys);
		memset(pc, 0, sizeof(*pc));
		if (!get_str(x, k, "name", pc->name, sizeof(pc->name), p) ||
		    !valid_id(pc->name, true)) {
			err(x, p, "«name»: строчная латиница, цифры и _");
		}
		get_int(x, k, "size", 11, 5, 11, &size, p);
		if (size != 5 && size != 11) {
			err(x, p, "«size»: 11 или 5");
			continue;
		}
		for (int q = 0; q < x->c->n_pictos; q++) {
			if (strcmp(x->c->pictos[q].name, pc->name) == 0 &&
			    x->c->pictos[q].bm.w == size) {
				err(x, p, "пиктограмма «%s» %ld×%ld уже есть", pc->name, (long)size,
				    (long)size);
			}
		}
		pc->bm.w = (uint8_t)size;
		pc->bm.h = (uint8_t)size;
		int rows = ws_json_get(x->j, k, "rows");

		if (!ws_json_is(x->j, rows, WS_J_ARR) || x->j->t[rows].size != size) {
			err(x, p, "«rows»: %ld строк в hex", (long)size);
			continue;
		}
		int y = 0;

		for (int r = ws_json_first(x->j, rows); r >= 0;
		     r = ws_json_next(x->j, rows, r), y++) {
			char hex[8];
			unsigned int bits = 0;
			char *end = NULL;

			if (ws_json_str(x->j, r, hex, sizeof(hex)) < 1) {
				err(x, p, "строка %d: hex", y);
				break;
			}
			bits = (unsigned int)strtoul(hex, &end, 16);
			if (*end || bits >= (1u << size)) {
				err(x, p, "строка %d: «%s» не помещается в %ld точек", y, hex,
				    (long)size);
				break;
			}
			pc->bm.rows[y] = (uint16_t)bits;
		}
		x->c->n_pictos++;
	}
}

static void parse_ext(struct ctx *x, int arr)
{
	char p[64];
	int i = 0;

	if (arr < 0) {
		return;
	}
	if (!ws_json_is(x->j, arr, WS_J_ARR)) {
		err(x, "ext", "должно быть списком");
		return;
	}
	if (x->j->t[arr].size > WS_MAX_EXT) {
		err(x, "ext", "внешних переменных больше %d", WS_MAX_EXT);
		return;
	}
	for (int k = ws_json_first(x->j, arr); k >= 0; k = ws_json_next(x->j, arr, k), i++) {
		static const char *const keys[] = {"id",   "name", "topic", "field",
						   "unit", "ttl",  NULL};
		struct ws_ext *e = &x->c->ext[x->c->n_ext];
		char id[12];
		int32_t ttl;

		snprintf(p, sizeof(p), "ext[%d]", i);
		if (!ws_json_is(x->j, k, WS_J_OBJ)) {
			err(x, p, "должно быть объектом");
			continue;
		}
		check_keys(x, k, p, keys);
		memset(e, 0, sizeof(*e));
		get_str(x, k, "id", id, sizeof(id), p);
		if (strncmp(id, "ext.", 4) != 0 || id[4] < '1' || id[4] > '8' || id[5]) {
			err(x, p, "«id»: ext.1 … ext.8");
			continue;
		}
		e->idx = (uint8_t)(id[4] - '0');
		for (int q = 0; q < x->c->n_ext; q++) {
			if (x->c->ext[q].idx == e->idx) {
				err(x, p, "«%s» уже описана", id);
			}
		}
		get_str(x, k, "name", e->name, sizeof(e->name), p);
		if (!get_str(x, k, "topic", e->topic, sizeof(e->topic), p) || !e->topic[0]) {
			err(x, p, "нужен MQTT-топик «topic»");
		}
		get_str(x, k, "field", e->field, sizeof(e->field), p);
		get_str(x, k, "unit", e->unit, sizeof(e->unit), p);
		get_int(x, k, "ttl", 3600, 0, 7 * 86400, &ttl, p);
		e->ttl = (uint32_t)ttl;
		x->c->n_ext++;
	}
}

int ws_cfg_compile(const char *json, size_t len, struct ws_config *out, struct ws_cfg_errors *errs,
		   struct ws_jtok *toks, int max_toks)
{
	struct ws_json j;
	struct ctx x = {.j = &j, .c = out, .e = errs};

	memset(errs, 0, sizeof(*errs));
	memset(out, 0, sizeof(*out));
	out->n_str = 1; /* offset 0 = "" */

	if (len > WS_MAX_FILE) {
		err(&x, "", "файл больше %d КБ", WS_MAX_FILE / 1024);
		return -1;
	}
	int r = ws_json_parse(&j, json, len, toks, max_toks);

	if (r < 0) {
		err(&x, "",
		    r == WS_JSON_EPART    ? "JSON обрезан (позиция %d)"
		    : r == WS_JSON_ENOMEM ? "JSON слишком сложный (позиция %d)"
					  : "ошибка JSON в позиции %d",
		    j.err_pos);
		return -1;
	}
	if (!ws_json_is(&j, 0, WS_J_OBJ)) {
		err(&x, "", "ожидается объект");
		return -1;
	}
	static const char *const keys[] = {"schema", "screens", "ext", "pictos", NULL};

	check_keys(&x, 0, "", keys);
	int32_t schema;

	if (!ws_json_int(&j, ws_json_get(&j, 0, "schema"), &schema) || schema != 1) {
		err(&x, "schema", "поддерживается только схема 1");
		return -1;
	}
	parse_pictos(&x, ws_json_get(&j, 0, "pictos"));
	parse_ext(&x, ws_json_get(&j, 0, "ext"));

	int screens = ws_json_get(&j, 0, "screens");

	if (!ws_json_is(&j, screens, WS_J_ARR) || j.t[screens].size == 0) {
		err(&x, "screens", "нужен хотя бы один экран");
		return -1;
	}
	if (j.t[screens].size > WS_MAX_SCREENS) {
		err(&x, "screens", "экранов больше %d", WS_MAX_SCREENS);
		return -1;
	}
	int i = 0, defaults = 0;
	char p[32];

	for (int k = ws_json_first(&j, screens); k >= 0; k = ws_json_next(&j, screens, k), i++) {
		snprintf(p, sizeof(p), "screens[%d]", i);
		parse_screen(&x, k, i, p);
		out->n_screens++;
		if (out->screens[i].is_default) {
			defaults++;
			out->def = (uint8_t)i;
		}
	}
	if (defaults != 1) {
		err(&x, "screens", "экран по умолчанию должен быть ровно один, а их %d", defaults);
	}
	return errs->count ? -1 : 0;
}

void ws_cfg_errors_json(const struct ws_cfg_errors *errs, struct ws_jw *w)
{
	int n = errs->count < WS_MAX_ERRORS ? errs->count : WS_MAX_ERRORS;

	ws_jw_arr(w);
	for (int i = 0; i < n; i++) {
		ws_jw_obj(w);
		ws_jw_kstr(w, "path", errs->e[i].path);
		ws_jw_kstr(w, "msg", errs->e[i].msg);
		ws_jw_obj_end(w);
	}
	if (errs->count > n) {
		ws_jw_obj(w);
		ws_jw_kstr(w, "path", "");
		ws_jw_kstr(w, "msg", "и другие ошибки");
		ws_jw_obj_end(w);
	}
	ws_jw_arr_end(w);
}

/* ---- serializer ---- */

static void w_value(struct ws_jw *w, int var, int32_t v)
{
	const struct ws_var_info *inf = ws_var_info(var);

	switch (inf->type) {
	case WS_VT_BOOL:
		ws_jw_bool(w, v != 0);
		break;
	case WS_VT_COND:
		ws_jw_str(w, ws_cond_name(v));
		break;
	case WS_VT_DIR:
		ws_jw_str(w, ws_dir_name(v));
		break;
	default:
		ws_jw_deci(w, v);
		break;
	}
}

static void w_cond(struct ws_jw *w, const struct ws_config *c, const struct ws_condition *cd)
{
	ws_jw_key(w, cd->any ? "any" : "all");
	ws_jw_arr(w);
	for (int i = 0; i < cd->n; i++) {
		const struct ws_cmp *m = &c->cmps[cd->first + i];

		ws_jw_obj(w);
		if (m->op == WS_OP_GROUP) {
			struct ws_condition sub = {(uint16_t)m->v[0], (uint8_t)m->v[1],
						   (uint8_t)m->v[2]};

			w_cond(w, c, &sub);
		} else {
			ws_jw_kstr(w, "var", ws_var_name(m->var));
			ws_jw_kstr(w, "op", op_names[m->op]);
			ws_jw_key(w, "val");
			if (m->op == WS_OP_BETWEEN || m->op == WS_OP_IN) {
				ws_jw_arr(w);
				for (int k = 0; k < m->n; k++) {
					w_value(w, m->var, m->v[k]);
				}
				ws_jw_arr_end(w);
			} else {
				w_value(w, m->var, m->v[0]);
			}
			if (m->hyst) {
				ws_jw_kdeci(w, "hyst", m->hyst);
			}
		}
		ws_jw_obj_end(w);
	}
	ws_jw_arr_end(w);
}

static const char *const align_names[] = {"left", "center", "right"};

static void w_item(struct ws_jw *w, const struct ws_config *c, const struct ws_item *it, bool alt)
{
	ws_jw_obj(w);
	ws_jw_kstr(w, "type", type_names[it->type]);
	if (it->type != WS_IT_ROTATOR) {
		ws_jw_kstr(w, "form", form_names[it->form]);
	}
	if (it->var != WS_V_NONE) {
		ws_jw_kstr(w, "var", ws_var_name(it->var));
	}
	if (!alt) {
		ws_jw_kint(w, "x", it->x);
		ws_jw_kint(w, "y", it->y);
		ws_jw_kint(w, "w", it->w);
		if (it->type == WS_IT_ROTATOR) {
			ws_jw_kint(w, "h", it->h);
		}
	}
	ws_jw_kstr(w, "align", align_names[it->align]);
	switch (it->type) {
	case WS_IT_TEMP:
		ws_jw_kbool(w, "plus", it->flags & WS_F_PLUS);
		ws_jw_kbool(w, "tenths", it->flags & WS_F_TENTHS);
		ws_jw_kbool(w, "deg", it->flags & WS_F_DEG);
		break;
	case WS_IT_NUMBER:
		ws_jw_kint(w, "decimals", it->decimals);
		ws_jw_kint(w, "digits", it->digits);
		if (it->prefix) {
			ws_jw_kstr(w, "prefix", ws_cfg_str(c, it->prefix));
		}
		if (it->suffix) {
			ws_jw_kstr(w, "suffix", ws_cfg_str(c, it->suffix));
		}
		if (it->picto != WS_NO_IDX) {
			ws_jw_kstr(w, "picto", ws_cfg_str(c, it->picto));
		}
		break;
	case WS_IT_RAIN:
		ws_jw_kstr(w, "none", ws_cfg_str(c, it->text));
		ws_jw_kint(w, "hours", it->hours);
		break;
	case WS_IT_WIND:
		ws_jw_kstr(w, "dir", (it->flags & WS_F_DIR_FROM) ? "from" : "to");
		break;
	case WS_IT_RANGE:
	case WS_IT_GRAPH:
		ws_jw_kint(w, "hours", it->hours);
		break;
	case WS_IT_PRESSURE:
		ws_jw_kdeci(w, "trend", it->thr);
		break;
	case WS_IT_CO2:
		ws_jw_kint(w, "invert", it->thr / 10);
		break;
	case WS_IT_TEXT:
		ws_jw_kstr(w, "text", ws_cfg_str(c, it->text));
		break;
	case WS_IT_PICTO:
		ws_jw_kstr(w, "name", ws_cfg_str(c, it->text));
		break;
	case WS_IT_SEP:
		ws_jw_kbool(w, "dashed", it->flags & WS_F_DASHED);
		break;
	case WS_IT_ROTATOR:
		ws_jw_kint(w, "period", it->period);
		ws_jw_kint(w, "offset", it->offset);
		break;
	default:
		break;
	}
	ws_jw_kbool(w, "skip", it->flags & WS_F_SKIP);
	if (it->has_when) {
		ws_jw_key(w, "when");
		ws_jw_obj(w);
		w_cond(w, c, &it->when);
		ws_jw_obj_end(w);
	}
	if (it->n_alts) {
		ws_jw_key(w, "alts");
		ws_jw_arr(w);
		for (int i = 0; i < it->n_alts; i++) {
			w_item(w, c, &c->items[it->alts + i], true);
		}
		ws_jw_arr_end(w);
	}
	if (it->type == WS_IT_ROTATOR) {
		ws_jw_key(w, "items");
		ws_jw_arr(w);
		for (int i = 0; i < it->n_children; i++) {
			w_item(w, c, &c->items[it->children + i], false);
		}
		ws_jw_arr_end(w);
	}
	ws_jw_obj_end(w);
}

static void w_time(struct ws_jw *w, const char *key, uint16_t m)
{
	char s[8];

	snprintf(s, sizeof(s), "%02u:%02u", (unsigned int)(m / 60) % 24, (unsigned int)m % 60);
	ws_jw_kstr(w, key, s);
}

int ws_cfg_to_json(const struct ws_config *c, char *buf, size_t len)
{
	struct ws_jw w;

	ws_jw_init(&w, buf, len);
	ws_jw_obj(&w);
	ws_jw_kint(&w, "schema", 1);
	ws_jw_key(&w, "screens");
	ws_jw_arr(&w);
	for (int s = 0; s < c->n_screens; s++) {
		const struct ws_screen *sc = &c->screens[s];

		ws_jw_obj(&w);
		ws_jw_kstr(&w, "id", sc->id);
		ws_jw_kstr(&w, "name", sc->name);
		ws_jw_kbool(&w, "default", sc->is_default);
		ws_jw_kbool(&w, "enabled", sc->enabled);
		if (sc->rule.present) {
			const struct ws_rule *r = &sc->rule;

			ws_jw_key(&w, "rule");
			ws_jw_obj(&w);
			if (r->cond.n) {
				w_cond(&w, c, &r->cond);
			}
			if (r->has_time) {
				ws_jw_key(&w, "time");
				ws_jw_obj(&w);
				w_time(&w, "from", r->from_min);
				w_time(&w, "to", r->to_min);
				ws_jw_key(&w, "days");
				ws_jw_arr(&w);
				for (int d = 0; d < 7; d++) {
					if (r->days & (1 << d)) {
						ws_jw_int(&w, d + 1);
					}
				}
				ws_jw_arr_end(&w);
				ws_jw_obj_end(&w);
			}
			ws_jw_kint(&w, "prio", r->prio);
			ws_jw_kint(&w, "on_delay", r->on_delay);
			ws_jw_kint(&w, "off_delay", r->off_delay);
			ws_jw_kint(&w, "min_show", r->min_show);
			ws_jw_kstr(&w, "mode", r->mode == WS_MODE_INSERT ? "insert" : "while");
			if (r->mode == WS_MODE_INSERT) {
				ws_jw_key(&w, "insert");
				ws_jw_obj(&w);
				ws_jw_kint(&w, "show", r->insert_show);
				ws_jw_kint(&w, "every", r->insert_every);
				ws_jw_obj_end(&w);
			}
			ws_jw_obj_end(&w);
		}
		ws_jw_key(&w, "items");
		ws_jw_arr(&w);
		for (int i = 0; i < sc->n_items; i++) {
			w_item(&w, c, &c->items[sc->items + i], false);
		}
		ws_jw_arr_end(&w);
		ws_jw_obj_end(&w);
	}
	ws_jw_arr_end(&w);
	if (c->n_ext) {
		ws_jw_key(&w, "ext");
		ws_jw_arr(&w);
		for (int i = 0; i < c->n_ext; i++) {
			const struct ws_ext *e = &c->ext[i];
			char id[8];

			snprintf(id, sizeof(id), "ext.%u", e->idx);
			ws_jw_obj(&w);
			ws_jw_kstr(&w, "id", id);
			ws_jw_kstr(&w, "name", e->name);
			ws_jw_kstr(&w, "topic", e->topic);
			ws_jw_kstr(&w, "field", e->field);
			ws_jw_kstr(&w, "unit", e->unit);
			ws_jw_kint(&w, "ttl", e->ttl);
			ws_jw_obj_end(&w);
		}
		ws_jw_arr_end(&w);
	}
	if (c->n_pictos) {
		ws_jw_key(&w, "pictos");
		ws_jw_arr(&w);
		for (int i = 0; i < c->n_pictos; i++) {
			const struct ws_picto *p = &c->pictos[i];
			char hex[8];

			ws_jw_obj(&w);
			ws_jw_kstr(&w, "name", p->name);
			ws_jw_kint(&w, "size", p->bm.w);
			ws_jw_key(&w, "rows");
			ws_jw_arr(&w);
			for (int y = 0; y < p->bm.h; y++) {
				snprintf(hex, sizeof(hex), "%04x", p->bm.rows[y]);
				ws_jw_str(&w, hex);
			}
			ws_jw_arr_end(&w);
			ws_jw_obj_end(&w);
		}
		ws_jw_arr_end(&w);
	}
	ws_jw_obj_end(&w);
	return w.ok ? (int)w.pos : -1;
}

/* ---- catalogue for the editor (GET /api/catalog) ---- */

struct type_meta {
	const char *label;
	const char *group;
	const char *params; /* comma separated parameter names */
};

static const struct type_meta type_meta[WS_IT_COUNT] = {
	[WS_IT_TEMP] = {"Температура", "Улица", "var,plus,tenths,deg,align"},
	[WS_IT_ICON] = {"Иконка погоды", "Улица", "var,align"},
	[WS_IT_NUMBER] = {"Число", "Дома", "var,decimals,digits,prefix,suffix,picto,align"},
	[WS_IT_RAIN] = {"Осадки", "Прогноз", "none,hours,skip,align"},
	[WS_IT_WIND] = {"Ветер", "Улица", "var,dir,align"},
	[WS_IT_RANGE] = {"Макс/мин", "Прогноз", "hours,align"},
	[WS_IT_PRESSURE] = {"Давление", "Дома", "var,trend,align"},
	[WS_IT_HUMIDITY] = {"Влажность", "Дома", "var,align"},
	[WS_IT_CO2] = {"CO2", "Дома", "invert,align"},
	[WS_IT_GRAPH] = {"График", "Прогноз", "hours"},
	[WS_IT_CLOCK] = {"Часы", "Время", "align"},
	[WS_IT_TEXT] = {"Текст", "Оформление", "text,align"},
	[WS_IT_PICTO] = {"Пиктограмма", "Оформление", "name,align"},
	[WS_IT_SEP] = {"Разделитель", "Оформление", "dashed"},
	[WS_IT_ROTATOR] = {"Зона ротации", "Оформление", "period,offset"},
};

int ws_catalog_json(const struct ws_config *cfg, char *buf, size_t len)
{
	struct ws_jw w;
	char p[96];

	ws_jw_init(&w, buf, len);
	ws_jw_obj(&w);
	ws_jw_key(&w, "types");
	ws_jw_arr(&w);
	for (int t = 0; t < WS_IT_COUNT; t++) {
		ws_jw_obj(&w);
		ws_jw_kstr(&w, "type", type_names[t]);
		ws_jw_kstr(&w, "label", type_meta[t].label);
		ws_jw_kstr(&w, "group", type_meta[t].group);
		ws_jw_kstr(&w, "form", form_names[form_default[t]]);
		ws_jw_key(&w, "forms");
		ws_jw_arr(&w);
		for (int f = 0; f <= WS_FORM_S2; f++) {
			if (forms_allowed[t] & (1 << f)) {
				ws_jw_str(&w, form_names[f]);
			}
		}
		ws_jw_arr_end(&w);
		ws_jw_key(&w, "var");
		if (var_default[t] != WS_V_NONE) {
			ws_jw_str(&w, ws_var_name(var_default[t]));
		} else {
			ws_jw_null(&w);
		}
		ws_jw_key(&w, "vars");
		ws_jw_arr(&w);
		for (int v = 0; v < WS_V_COUNT; v++) {
			if (var_default[t] != WS_V_NONE || t == WS_IT_NUMBER) {
				if (var_fits(t, v)) {
					ws_jw_str(&w, ws_var_name(v));
				}
			}
		}
		ws_jw_arr_end(&w);
		ws_jw_key(&w, "params");
		ws_jw_arr(&w);
		snprintf(p, sizeof(p), "%s", type_meta[t].params);
		for (char *s = strtok(p, ","); s; s = strtok(NULL, ",")) {
			ws_jw_str(&w, s);
		}
		ws_jw_arr_end(&w);
		ws_jw_obj_end(&w);
	}
	ws_jw_arr_end(&w);
	ws_jw_key(&w, "pictos");
	ws_jw_arr(&w);
	for (size_t i = 0; i < ws_picto_builtin_count(); i++) {
		ws_jw_str(&w, ws_picto_builtin_name(i));
	}
	for (int i = 0; cfg && i < cfg->n_pictos; i++) {
		ws_jw_str(&w, cfg->pictos[i].name);
	}
	ws_jw_arr_end(&w);
	ws_jw_key(&w, "conds");
	ws_jw_arr(&w);
	for (int i = 0; i < WS_COND_COUNT; i++) {
		ws_jw_str(&w, ws_cond_name(i));
	}
	ws_jw_arr_end(&w);
	ws_jw_key(&w, "limits");
	ws_jw_obj(&w);
	ws_jw_kint(&w, "screens", WS_MAX_SCREENS);
	ws_jw_kint(&w, "items", WS_MAX_ITEMS);
	ws_jw_kint(&w, "rotators", WS_MAX_ROTATORS);
	ws_jw_kint(&w, "rotator_items", WS_MAX_ROT_ITEMS);
	ws_jw_kint(&w, "rule_cmps", WS_MAX_RULE_CMPS);
	ws_jw_kint(&w, "when_cmps", WS_MAX_WHEN_CMPS);
	ws_jw_kint(&w, "alts", WS_MAX_ALTS);
	ws_jw_kint(&w, "ext", WS_MAX_EXT);
	ws_jw_kint(&w, "pictos", WS_MAX_PICTOS);
	ws_jw_kint(&w, "file", WS_MAX_FILE);
	ws_jw_obj_end(&w);
	ws_jw_obj_end(&w);
	return w.ok ? (int)w.pos : -1;
}
