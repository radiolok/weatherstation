/*
 * Small JSON tokenizer and writer (no allocation).
 *
 * The tokenizer keeps the whole document as an array of tokens in pre-order.
 * Every token knows the index right after its subtree, so objects and arrays
 * are walked without recursion:
 *
 *   for (int i = ws_json_first(t, obj); i >= 0; i = ws_json_next(t, obj, i))
 */
#ifndef WS_JSON_H_
#define WS_JSON_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ws_jtype {
	WS_J_NONE,
	WS_J_OBJ,
	WS_J_ARR,
	WS_J_STR,
	WS_J_NUM,
	WS_J_TRUE,
	WS_J_FALSE,
	WS_J_NULL,
};

/* Documents up to 64 KB: offsets are 16 bit to keep a token at 10 bytes. */
#define WS_JSON_MAX_LEN 65535

struct ws_jtok {
	uint8_t type;
	uint16_t start; /* first byte (for strings: after the quote) */
	uint16_t end;   /* one past the last byte (for strings: the closing quote) */
	uint16_t size;  /* children: members of an object (key+value pairs), items of an array */
	uint16_t skip;  /* index of the token after this subtree */
};

struct ws_json {
	const char *js;
	size_t len;
	struct ws_jtok *t;
	int count;
	int err_pos; /* byte offset of a parse error */
};

#define WS_JSON_ENOMEM -1
#define WS_JSON_EINVAL -2
#define WS_JSON_EPART  -3

/* Parses js[0..len). Object members are stored as key token followed by the
 * value token. Returns the token count or a negative error. */
int ws_json_parse(struct ws_json *j, const char *js, size_t len, struct ws_jtok *toks,
		  int max_toks);

/* Iteration over the values of an array or the keys of an object. */
int ws_json_first(const struct ws_json *j, int parent);
int ws_json_next(const struct ws_json *j, int parent, int cur);

/* Object member value by key, -1 if missing or `obj` is not an object. */
int ws_json_get(const struct ws_json *j, int obj, const char *key);
/* Array item, -1 if out of range. */
int ws_json_at(const struct ws_json *j, int arr, int idx);

bool ws_json_is(const struct ws_json *j, int tok, enum ws_jtype type);
bool ws_json_key_eq(const struct ws_json *j, int tok, const char *s);

/* Copies an unescaped string (UTF-8), returns length or -1 if not a string
 * or it does not fit. */
int ws_json_str(const struct ws_json *j, int tok, char *buf, size_t len);
/* Integer value, false if not an integer number. */
bool ws_json_int(const struct ws_json *j, int tok, int32_t *out);
/* Number scaled by 10 and rounded half away from zero ("12.34" -> 123). */
bool ws_json_deci(const struct ws_json *j, int tok, int32_t *out);
bool ws_json_bool(const struct ws_json *j, int tok, bool *out);

/* Writer into a caller buffer; on overflow `ok` becomes false and the output
 * is truncated but still NUL-terminated. */
struct ws_jw {
	char *buf;
	size_t cap;
	size_t pos;
	bool ok;
	uint8_t depth;
	uint32_t first; /* bit per depth: no element written yet */
};

void ws_jw_init(struct ws_jw *w, char *buf, size_t cap);
void ws_jw_obj(struct ws_jw *w);
void ws_jw_obj_end(struct ws_jw *w);
void ws_jw_arr(struct ws_jw *w);
void ws_jw_arr_end(struct ws_jw *w);
void ws_jw_key(struct ws_jw *w, const char *key);
void ws_jw_str(struct ws_jw *w, const char *s);
void ws_jw_int(struct ws_jw *w, int64_t v);
void ws_jw_deci(struct ws_jw *w, int32_t deci); /* 125 -> 12.5, 120 -> 12 */
void ws_jw_bool(struct ws_jw *w, bool v);
void ws_jw_null(struct ws_jw *w);
/* Raw, already valid JSON value. */
void ws_jw_raw(struct ws_jw *w, const char *json, size_t len);

/* Shorthands for "key": value */
void ws_jw_kstr(struct ws_jw *w, const char *key, const char *s);
void ws_jw_kint(struct ws_jw *w, const char *key, int64_t v);
void ws_jw_kdeci(struct ws_jw *w, const char *key, int32_t deci);
void ws_jw_kbool(struct ws_jw *w, const char *key, bool v);

#ifdef __cplusplus
}
#endif

#endif /* WS_JSON_H_ */
