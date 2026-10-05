/* JSON tokenizer and writer, see ws/json.h. */
#include <stdio.h>
#include <string.h>

#include <ws/json.h>

#define MAX_DEPTH 16

struct parser {
	struct ws_json *j;
	int max;
	size_t pos;
	int depth;
};

static void skip_ws(struct parser *p)
{
	while (p->pos < p->j->len) {
		char c = p->j->js[p->pos];

		if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
			break;
		}
		p->pos++;
	}
}

static int new_tok(struct parser *p, enum ws_jtype type, size_t start)
{
	if (p->j->count >= p->max) {
		return WS_JSON_ENOMEM;
	}
	struct ws_jtok *t = &p->j->t[p->j->count];

	t->type = type;
	t->start = (uint16_t)start;
	t->end = (uint16_t)start;
	t->size = 0;
	t->skip = 0;
	return p->j->count++;
}

static int fail(struct parser *p, int err)
{
	p->j->err_pos = (int)p->pos;
	return err;
}

static int parse_value(struct parser *p);

static int parse_string(struct parser *p)
{
	/* at the opening quote */
	size_t start = ++p->pos;

	while (p->pos < p->j->len) {
		unsigned char c = (unsigned char)p->j->js[p->pos];

		if (c == '"') {
			int i = new_tok(p, WS_J_STR, start);

			if (i < 0) {
				return fail(p, i);
			}
			p->j->t[i].end = (uint16_t)p->pos;
			p->j->t[i].skip = (uint16_t)(i + 1);
			p->pos++;
			return i;
		}
		if (c < 0x20) {
			return fail(p, WS_JSON_EINVAL);
		}
		if (c == '\\') {
			p->pos++;
			if (p->pos >= p->j->len) {
				break;
			}
			c = (unsigned char)p->j->js[p->pos];
			if (c == 'u') {
				for (int k = 0; k < 4; k++) {
					p->pos++;
					if (p->pos >= p->j->len) {
						return fail(p, WS_JSON_EPART);
					}
					char h = p->j->js[p->pos];

					if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') ||
					      (h >= 'A' && h <= 'F'))) {
						return fail(p, WS_JSON_EINVAL);
					}
				}
			} else if (!strchr("\"\\/bfnrt", c)) {
				return fail(p, WS_JSON_EINVAL);
			}
		}
		p->pos++;
	}
	return fail(p, WS_JSON_EPART);
}

static int parse_number(struct parser *p)
{
	size_t start = p->pos;
	const char *s = p->j->js;
	size_t n = p->j->len;

	if (p->pos < n && s[p->pos] == '-') {
		p->pos++;
	}
	if (p->pos >= n || s[p->pos] < '0' || s[p->pos] > '9') {
		return fail(p, p->pos >= n ? WS_JSON_EPART : WS_JSON_EINVAL);
	}
	while (p->pos < n && s[p->pos] >= '0' && s[p->pos] <= '9') {
		p->pos++;
	}
	if (p->pos < n && s[p->pos] == '.') {
		p->pos++;
		if (p->pos >= n || s[p->pos] < '0' || s[p->pos] > '9') {
			return fail(p, WS_JSON_EINVAL);
		}
		while (p->pos < n && s[p->pos] >= '0' && s[p->pos] <= '9') {
			p->pos++;
		}
	}
	if (p->pos < n && (s[p->pos] == 'e' || s[p->pos] == 'E')) {
		p->pos++;
		if (p->pos < n && (s[p->pos] == '+' || s[p->pos] == '-')) {
			p->pos++;
		}
		if (p->pos >= n || s[p->pos] < '0' || s[p->pos] > '9') {
			return fail(p, WS_JSON_EINVAL);
		}
		while (p->pos < n && s[p->pos] >= '0' && s[p->pos] <= '9') {
			p->pos++;
		}
	}
	int i = new_tok(p, WS_J_NUM, start);

	if (i < 0) {
		return fail(p, i);
	}
	p->j->t[i].end = (uint16_t)p->pos;
	p->j->t[i].skip = (uint16_t)(i + 1);
	return i;
}

static int parse_literal(struct parser *p, const char *lit, enum ws_jtype type)
{
	size_t l = strlen(lit);

	if (p->pos + l > p->j->len) {
		return fail(p, strncmp(p->j->js + p->pos, lit, p->j->len - p->pos) == 0
				       ? WS_JSON_EPART
				       : WS_JSON_EINVAL);
	}
	if (strncmp(p->j->js + p->pos, lit, l) != 0) {
		return fail(p, WS_JSON_EINVAL);
	}
	int i = new_tok(p, type, p->pos);

	if (i < 0) {
		return fail(p, i);
	}
	p->pos += l;
	p->j->t[i].end = (uint16_t)p->pos;
	p->j->t[i].skip = (uint16_t)(i + 1);
	return i;
}

static int parse_container(struct parser *p, bool obj)
{
	int i = new_tok(p, obj ? WS_J_OBJ : WS_J_ARR, p->pos);

	if (i < 0) {
		return fail(p, i);
	}
	if (++p->depth > MAX_DEPTH) {
		return fail(p, WS_JSON_EINVAL);
	}
	p->pos++;
	skip_ws(p);
	char close = obj ? '}' : ']';

	if (p->pos < p->j->len && p->j->js[p->pos] == close) {
		p->pos++;
		goto done;
	}
	for (;;) {
		skip_ws(p);
		if (p->pos >= p->j->len) {
			return fail(p, WS_JSON_EPART);
		}
		if (obj) {
			if (p->j->js[p->pos] != '"') {
				return fail(p, WS_JSON_EINVAL);
			}
			int k = parse_string(p);

			if (k < 0) {
				return k;
			}
			skip_ws(p);
			if (p->pos >= p->j->len) {
				return fail(p, WS_JSON_EPART);
			}
			if (p->j->js[p->pos] != ':') {
				return fail(p, WS_JSON_EINVAL);
			}
			p->pos++;
		}
		int v = parse_value(p);

		if (v < 0) {
			return v;
		}
		if (p->j->t[i].size == UINT16_MAX) {
			return fail(p, WS_JSON_ENOMEM);
		}
		p->j->t[i].size++;
		skip_ws(p);
		if (p->pos >= p->j->len) {
			return fail(p, WS_JSON_EPART);
		}
		char c = p->j->js[p->pos++];

		if (c == close) {
			break;
		}
		if (c != ',') {
			p->pos--;
			return fail(p, WS_JSON_EINVAL);
		}
	}
done:
	p->depth--;
	p->j->t[i].end = (uint16_t)p->pos;
	p->j->t[i].skip = (uint16_t)p->j->count;
	return i;
}

static int parse_value(struct parser *p)
{
	skip_ws(p);
	if (p->pos >= p->j->len) {
		return fail(p, WS_JSON_EPART);
	}
	char c = p->j->js[p->pos];

	switch (c) {
	case '{':
		return parse_container(p, true);
	case '[':
		return parse_container(p, false);
	case '"':
		return parse_string(p);
	case 't':
		return parse_literal(p, "true", WS_J_TRUE);
	case 'f':
		return parse_literal(p, "false", WS_J_FALSE);
	case 'n':
		return parse_literal(p, "null", WS_J_NULL);
	default:
		if (c == '-' || (c >= '0' && c <= '9')) {
			return parse_number(p);
		}
		return fail(p, WS_JSON_EINVAL);
	}
}

int ws_json_parse(struct ws_json *j, const char *js, size_t len, struct ws_jtok *toks, int max_toks)
{
	struct parser p = {.j = j, .max = max_toks > UINT16_MAX ? UINT16_MAX : max_toks};

	j->js = js;
	j->len = len;
	j->t = toks;
	j->count = 0;
	j->err_pos = -1;
	if (len > WS_JSON_MAX_LEN) {
		return WS_JSON_ENOMEM;
	}
	int r = parse_value(&p);

	if (r < 0) {
		j->count = 0;
		return r;
	}
	skip_ws(&p);
	if (p.pos != len) {
		j->err_pos = (int)p.pos;
		j->count = 0;
		return WS_JSON_EINVAL;
	}
	return j->count;
}

int ws_json_first(const struct ws_json *j, int parent)
{
	if (parent < 0 || parent >= j->count || j->t[parent].size == 0) {
		return -1;
	}
	return parent + 1;
}

int ws_json_next(const struct ws_json *j, int parent, int cur)
{
	int n;

	if (j->t[parent].type == WS_J_OBJ) {
		/* cur is a key: skip key and value */
		int v = cur + 1;

		n = j->t[v].skip;
	} else {
		n = j->t[cur].skip;
	}
	return n < j->t[parent].skip ? n : -1;
}

bool ws_json_is(const struct ws_json *j, int tok, enum ws_jtype type)
{
	return tok >= 0 && tok < j->count && j->t[tok].type == type;
}

bool ws_json_key_eq(const struct ws_json *j, int tok, const char *s)
{
	if (!ws_json_is(j, tok, WS_J_STR)) {
		return false;
	}
	size_t l = j->t[tok].end - j->t[tok].start;

	return strlen(s) == l && memcmp(j->js + j->t[tok].start, s, l) == 0;
}

int ws_json_get(const struct ws_json *j, int obj, const char *key)
{
	if (!ws_json_is(j, obj, WS_J_OBJ)) {
		return -1;
	}
	for (int k = ws_json_first(j, obj); k >= 0; k = ws_json_next(j, obj, k)) {
		if (ws_json_key_eq(j, k, key)) {
			return k + 1;
		}
	}
	return -1;
}

int ws_json_at(const struct ws_json *j, int arr, int idx)
{
	if (!ws_json_is(j, arr, WS_J_ARR) || idx < 0) {
		return -1;
	}
	int n = 0;

	for (int k = ws_json_first(j, arr); k >= 0; k = ws_json_next(j, arr, k)) {
		if (n++ == idx) {
			return k;
		}
	}
	return -1;
}

static int hexval(char h)
{
	if (h >= '0' && h <= '9') {
		return h - '0';
	}
	if (h >= 'a' && h <= 'f') {
		return h - 'a' + 10;
	}
	return h - 'A' + 10;
}

static int put_utf8(char *buf, size_t len, size_t *o, uint32_t cp)
{
	char tmp[4];
	int n;

	if (cp < 0x80) {
		tmp[0] = (char)cp;
		n = 1;
	} else if (cp < 0x800) {
		tmp[0] = (char)(0xC0 | (cp >> 6));
		tmp[1] = (char)(0x80 | (cp & 0x3F));
		n = 2;
	} else if (cp < 0x10000) {
		tmp[0] = (char)(0xE0 | (cp >> 12));
		tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		tmp[2] = (char)(0x80 | (cp & 0x3F));
		n = 3;
	} else {
		tmp[0] = (char)(0xF0 | (cp >> 18));
		tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
		tmp[3] = (char)(0x80 | (cp & 0x3F));
		n = 4;
	}
	if (*o + n >= len) {
		return -1;
	}
	memcpy(buf + *o, tmp, n);
	*o += n;
	return 0;
}

int ws_json_str(const struct ws_json *j, int tok, char *buf, size_t len)
{
	if (!ws_json_is(j, tok, WS_J_STR) || len == 0) {
		return -1;
	}
	size_t o = 0;
	const char *s = j->js;

	for (uint32_t i = j->t[tok].start; i < j->t[tok].end; i++) {
		char c = s[i];
		uint32_t cp;

		if (c != '\\') {
			if (o + 1 >= len) {
				return -1;
			}
			buf[o++] = c;
			continue;
		}
		c = s[++i];
		switch (c) {
		case 'b':
			cp = '\b';
			break;
		case 'f':
			cp = '\f';
			break;
		case 'n':
			cp = '\n';
			break;
		case 'r':
			cp = '\r';
			break;
		case 't':
			cp = '\t';
			break;
		case 'u':
			cp = (uint16_t)(hexval(s[i + 1]) << 12 | hexval(s[i + 2]) << 8 |
					hexval(s[i + 3]) << 4 | hexval(s[i + 4]));
			i += 4;
			if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < j->t[tok].end &&
			    s[i + 1] == '\\' && s[i + 2] == 'u') {
				uint32_t lo =
					(uint16_t)(hexval(s[i + 3]) << 12 | hexval(s[i + 4]) << 8 |
						   hexval(s[i + 5]) << 4 | hexval(s[i + 6]));

				if (lo >= 0xDC00 && lo < 0xE000) {
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					i += 6;
				}
			}
			break;
		default: /* " \ / */
			cp = (unsigned char)c;
			break;
		}
		if (put_utf8(buf, len, &o, cp) < 0) {
			return -1;
		}
	}
	buf[o] = '\0';
	return (int)o;
}

bool ws_json_int(const struct ws_json *j, int tok, int32_t *out)
{
	if (!ws_json_is(j, tok, WS_J_NUM)) {
		return false;
	}
	const char *s = j->js + j->t[tok].start;
	const char *e = j->js + j->t[tok].end;
	bool neg = false;
	int64_t v = 0;

	if (s < e && *s == '-') {
		neg = true;
		s++;
	}
	for (; s < e; s++) {
		if (*s < '0' || *s > '9') {
			return false; /* fraction or exponent */
		}
		v = v * 10 + (*s - '0');
		if (v > INT32_MAX) {
			return false;
		}
	}
	*out = (int32_t)(neg ? -v : v);
	return true;
}

bool ws_json_deci(const struct ws_json *j, int tok, int32_t *out)
{
	if (!ws_json_is(j, tok, WS_J_NUM)) {
		return false;
	}
	const char *s = j->js + j->t[tok].start;
	const char *e = j->js + j->t[tok].end;
	bool neg = false;
	int64_t ip = 0;
	int frac1 = 0, frac2 = 0, nfrac = 0;
	int exp = 0;

	if (*s == '-') {
		neg = true;
		s++;
	}
	for (; s < e && *s >= '0' && *s <= '9'; s++) {
		ip = ip * 10 + (*s - '0');
		if (ip > 100000000) {
			return false;
		}
	}
	if (s < e && *s == '.') {
		for (s++; s < e && *s >= '0' && *s <= '9'; s++) {
			if (nfrac == 0) {
				frac1 = *s - '0';
			} else if (nfrac == 1) {
				frac2 = *s - '0';
			}
			nfrac++;
		}
	}
	if (s < e && (*s == 'e' || *s == 'E')) {
		bool eneg = false;

		s++;
		if (*s == '+' || *s == '-') {
			eneg = *s == '-';
			s++;
		}
		for (; s < e; s++) {
			exp = exp * 10 + (*s - '0');
			if (exp > 9) {
				return false;
			}
		}
		if (eneg) {
			exp = -exp;
		}
	}
	/* value * 100 as integer, then apply exponent, then round to tenths */
	int64_t v100 = ip * 100 + frac1 * 10 + frac2;

	for (; exp > 0; exp--) {
		v100 *= 10;
		if (v100 > INT32_MAX * 10LL) {
			return false;
		}
	}
	for (; exp < 0; exp++) {
		v100 /= 10;
	}
	int64_t d = (v100 + 5) / 10;

	if (d > INT32_MAX) {
		return false;
	}
	*out = (int32_t)(neg ? -d : d);
	return true;
}

bool ws_json_bool(const struct ws_json *j, int tok, bool *out)
{
	if (ws_json_is(j, tok, WS_J_TRUE)) {
		*out = true;
		return true;
	}
	if (ws_json_is(j, tok, WS_J_FALSE)) {
		*out = false;
		return true;
	}
	return false;
}

/* ---- writer ---- */

static void put(struct ws_jw *w, const char *s, size_t n)
{
	if (w->cap == 0) {
		w->ok = false;
		return;
	}
	if (w->pos + n + 1 > w->cap) {
		size_t room = w->cap - 1 - w->pos;

		memcpy(w->buf + w->pos, s, room);
		w->pos += room;
		w->buf[w->pos] = '\0';
		w->ok = false;
		return;
	}
	memcpy(w->buf + w->pos, s, n);
	w->pos += n;
	w->buf[w->pos] = '\0';
}

static void puts_(struct ws_jw *w, const char *s)
{
	put(w, s, strlen(s));
}

void ws_jw_init(struct ws_jw *w, char *buf, size_t cap)
{
	w->buf = buf;
	w->cap = cap;
	w->pos = 0;
	w->ok = cap > 0;
	w->depth = 0;
	w->first = 1;
	if (cap) {
		buf[0] = '\0';
	}
}

/* Separator before a value that is not an object member value. */
static void sep(struct ws_jw *w)
{
	uint32_t bit = 1u << w->depth;

	if (w->first & bit) {
		w->first &= ~bit;
	} else {
		put(w, ",", 1);
	}
}

static bool after_key(struct ws_jw *w)
{
	/* ws_jw_key leaves depth bit 31 set to say "value follows" */
	if (w->first & 0x80000000u) {
		w->first &= ~0x80000000u;
		return true;
	}
	return false;
}

static void value_prefix(struct ws_jw *w)
{
	if (!after_key(w)) {
		sep(w);
	}
}

static void open_(struct ws_jw *w, char c)
{
	value_prefix(w);
	put(w, &c, 1);
	if (w->depth < 30) {
		w->depth++;
	}
	w->first |= 1u << w->depth;
}

static void close_(struct ws_jw *w, char c)
{
	w->first &= ~(1u << w->depth);
	if (w->depth) {
		w->depth--;
	}
	put(w, &c, 1);
}

void ws_jw_obj(struct ws_jw *w)
{
	open_(w, '{');
}

void ws_jw_obj_end(struct ws_jw *w)
{
	close_(w, '}');
}

void ws_jw_arr(struct ws_jw *w)
{
	open_(w, '[');
}

void ws_jw_arr_end(struct ws_jw *w)
{
	close_(w, ']');
}

static void str_body(struct ws_jw *w, const char *s)
{
	put(w, "\"", 1);
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		char esc[8];

		if (c == '"' || c == '\\') {
			esc[0] = '\\';
			esc[1] = (char)c;
			put(w, esc, 2);
		} else if (c < 0x20) {
			snprintf(esc, sizeof(esc), "\\u%04x", c);
			put(w, esc, 6);
		} else {
			put(w, (const char *)&c, 1);
		}
	}
	put(w, "\"", 1);
}

void ws_jw_key(struct ws_jw *w, const char *key)
{
	sep(w);
	str_body(w, key);
	put(w, ":", 1);
	w->first |= 0x80000000u;
}

void ws_jw_str(struct ws_jw *w, const char *s)
{
	value_prefix(w);
	str_body(w, s);
}

void ws_jw_int(struct ws_jw *w, int64_t v)
{
	char b[24];

	value_prefix(w);
	snprintf(b, sizeof(b), "%lld", (long long)v);
	puts_(w, b);
}

void ws_jw_deci(struct ws_jw *w, int32_t deci)
{
	char b[24];
	int32_t a = deci < 0 ? -deci : deci;

	value_prefix(w);
	if (a % 10) {
		snprintf(b, sizeof(b), "%s%ld.%ld", deci < 0 ? "-" : "", (long)(a / 10),
			 (long)(a % 10));
	} else {
		snprintf(b, sizeof(b), "%ld", (long)(deci / 10));
	}
	puts_(w, b);
}

void ws_jw_bool(struct ws_jw *w, bool v)
{
	value_prefix(w);
	puts_(w, v ? "true" : "false");
}

void ws_jw_null(struct ws_jw *w)
{
	value_prefix(w);
	puts_(w, "null");
}

void ws_jw_raw(struct ws_jw *w, const char *json, size_t len)
{
	value_prefix(w);
	put(w, json, len);
}

void ws_jw_kstr(struct ws_jw *w, const char *key, const char *s)
{
	ws_jw_key(w, key);
	ws_jw_str(w, s);
}

void ws_jw_kint(struct ws_jw *w, const char *key, int64_t v)
{
	ws_jw_key(w, key);
	ws_jw_int(w, v);
}

void ws_jw_kdeci(struct ws_jw *w, const char *key, int32_t deci)
{
	ws_jw_key(w, key);
	ws_jw_deci(w, deci);
}

void ws_jw_kbool(struct ws_jw *w, const char *key, bool v)
{
	ws_jw_key(w, key);
	ws_jw_bool(w, v);
}
