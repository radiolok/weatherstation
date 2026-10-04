/*
 * Screen configuration: JSON schema 1 (docs/screen-constructor.md, 8)
 * compiled into flat structures. JSON is parsed only on load and save; the
 * screen selection and the renderer work with these structures.
 */
#ifndef WS_CONFIG_H_
#define WS_CONFIG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ws/json.h>
#include <ws/sign.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Limits from the spec */
#define WS_MAX_SCREENS   16
#define WS_MAX_ITEMS     12 /* top level items per screen, rotators included */
#define WS_MAX_ROTATORS  4
#define WS_MAX_ROT_ITEMS 8
#define WS_MAX_WHEN_CMPS 4
#define WS_MAX_ALTS      3
#define WS_MAX_RULE_CMPS 8
#define WS_MAX_EXT       8
#define WS_MAX_PICTOS    32
#define WS_MAX_FILE      (32 * 1024)
#define WS_MAX_LIST      4 /* values of the "in" operator */

/* Pool sizes of one compiled configuration (about 16 KB) */
#define WS_POOL_ITEMS   192
#define WS_POOL_CMPS    128
#define WS_POOL_STRINGS 1536

#define WS_ID_LEN      16
#define WS_NAME_LEN    32
#define WS_TOPIC_LEN   64
#define WS_FIELD_LEN   24
#define WS_UNIT_LEN    8
#define WS_JSON_TOKENS 4096 /* tokenizer buffer for a 32 KB file */

#define WS_NO_IDX 0xFFFF

enum ws_item_type {
	WS_IT_TEMP,
	WS_IT_ICON,
	WS_IT_NUMBER,
	WS_IT_RAIN,
	WS_IT_WIND,
	WS_IT_RANGE,
	WS_IT_PRESSURE,
	WS_IT_HUMIDITY,
	WS_IT_CO2,
	WS_IT_GRAPH,
	WS_IT_CLOCK,
	WS_IT_TEXT,
	WS_IT_PICTO,
	WS_IT_SEP,
	WS_IT_ROTATOR,
	WS_IT_COUNT
};

enum ws_form {
	WS_FORM_L,
	WS_FORM_S,
	WS_FORM_S2,
};

enum ws_align {
	WS_ALIGN_LEFT,
	WS_ALIGN_CENTER,
	WS_ALIGN_RIGHT,
};

enum ws_op {
	WS_OP_EQ,
	WS_OP_NE,
	WS_OP_LT,
	WS_OP_LE,
	WS_OP_GT,
	WS_OP_GE,
	WS_OP_BETWEEN,
	WS_OP_IN,
	WS_OP_GROUP, /* nested group: v[0] first cmp, v[1] count, v[2] 1 = any */
};

/* Item flags */
#define WS_F_PLUS     0x01 /* temp: "+" for positive values */
#define WS_F_TENTHS   0x02 /* temp: one decimal */
#define WS_F_SKIP     0x04 /* skip in a rotator when empty (e.g. no rain) */
#define WS_F_DASHED   0x08 /* sep: dashed line */
#define WS_F_DIR_FROM 0x10 /* wind: arrow where the wind comes from */
#define WS_F_DEG      0x20 /* temp: degree sign (default on) */
#define WS_F_INVERT   0x40 /* co2: invert the value at or above thr */

struct ws_cmp {
	uint8_t var;
	uint8_t op;
	uint8_t n;     /* list length for WS_OP_IN */
	uint8_t state; /* hysteresis: last result */
	int32_t v[WS_MAX_LIST];
	int32_t hyst;
};

/* Condition: comparisons cmps[first .. first + n), all or any of them. */
struct ws_condition {
	uint16_t first;
	uint8_t n;
	uint8_t any;
};

struct ws_item {
	uint8_t type;
	uint8_t form;
	uint8_t align;
	uint8_t flags;
	int16_t x, y, w, h;
	uint8_t var;      /* main variable or WS_V_NONE */
	uint8_t var2;     /* second variable (wind direction, pressure trend) */
	uint8_t decimals; /* number: digits after the point */
	uint8_t digits;   /* number: worst case digits for the default width */
	int32_t thr;      /* co2: inversion threshold; pressure: trend threshold (tenths) */
	uint16_t hours;   /* graph: points; range: horizon; rain: horizon text */
	uint16_t text;    /* text / picto name / none text (string pool offset) */
	uint16_t prefix;  /* number: prefix (string pool) */
	uint16_t suffix;  /* number: suffix (string pool) */
	uint16_t picto;   /* number: pictogram on the left (string pool), else WS_NO_IDX */
	struct ws_condition when;
	uint8_t has_when;
	uint8_t n_alts;
	uint16_t alts;     /* first alternative in the item pool */
	uint16_t children; /* rotator: first child in the item pool */
	uint8_t n_children;
	uint16_t period, offset; /* rotator, seconds */
};

enum ws_rule_mode {
	WS_MODE_WHILE,
	WS_MODE_INSERT,
};

struct ws_rule {
	bool present;
	struct ws_condition cond;
	bool has_time;
	uint16_t from_min, to_min; /* window, minutes of the day; through midnight if from > to */
	uint8_t days;              /* bit (dow - 1), dow 1 = Monday */
	uint8_t prio;              /* 1..99 */
	uint8_t mode;
	uint32_t on_delay, off_delay, min_show; /* seconds */
	uint16_t insert_show;                   /* seconds */
	uint16_t insert_every;                  /* minutes */
};

struct ws_screen {
	char id[WS_ID_LEN];
	char name[WS_NAME_LEN];
	bool is_default;
	bool enabled;
	struct ws_rule rule;
	uint16_t items; /* first top level item */
	uint8_t n_items;
};

struct ws_picto {
	char name[WS_ID_LEN];
	struct ws_bitmap bm;
};

struct ws_ext {
	uint8_t idx; /* 1..8 */
	char name[WS_NAME_LEN];
	char topic[WS_TOPIC_LEN];
	char field[WS_FIELD_LEN];
	char unit[WS_UNIT_LEN];
	uint32_t ttl;
};

struct ws_config {
	uint8_t n_screens;
	uint8_t def;
	struct ws_screen screens[WS_MAX_SCREENS];
	uint16_t n_items;
	struct ws_item items[WS_POOL_ITEMS];
	uint16_t n_cmps;
	struct ws_cmp cmps[WS_POOL_CMPS];
	uint16_t n_str;
	char str[WS_POOL_STRINGS];
	uint8_t n_pictos;
	struct ws_picto pictos[WS_MAX_PICTOS];
	uint8_t n_ext;
	struct ws_ext ext[WS_MAX_EXT];
};

#define WS_MAX_ERRORS 8

struct ws_cfg_error {
	char path[64];
	char msg[96];
};

struct ws_cfg_errors {
	int count;
	struct ws_cfg_error e[WS_MAX_ERRORS];
};

/* Validates and compiles `json` into `out`. `toks` is scratch space
 * (WS_JSON_TOKENS entries are enough for a 32 KB file). Returns 0, or -1 with
 * at least one error in `errs`; `out` is then unusable. */
int ws_cfg_compile(const char *json, size_t len, struct ws_config *out, struct ws_cfg_errors *errs,
		   struct ws_jtok *toks, int max_toks);

/* Canonical JSON of a compiled configuration (schema 1). Returns the length
 * or -1 if it does not fit. */
int ws_cfg_to_json(const struct ws_config *cfg, char *buf, size_t len);

/* Errors as a JSON array [{"path": ..., "msg": ...}] for the API. */
void ws_cfg_errors_json(const struct ws_cfg_errors *errs, struct ws_jw *w);

/* Lookups */
const char *ws_cfg_str(const struct ws_config *cfg, uint16_t off);
int ws_cfg_screen_by_id(const struct ws_config *cfg, const char *id);
/* User pictogram first, then the built-in one; NULL if neither exists. */
const struct ws_bitmap *ws_cfg_picto(const struct ws_config *cfg, const char *name, bool large);

const char *ws_item_type_name(int type);
const char *ws_form_name(int form);
const char *ws_op_name(int op);

/* Default frame width of an item (worst case value), and its height. */
int ws_item_default_w(const struct ws_config *cfg, const struct ws_item *it);
int ws_form_height(int form);

/* Element catalogue for the editor (GET /api/catalog). */
int ws_catalog_json(const struct ws_config *cfg, char *buf, size_t len);

/* Reset hysteresis state, e.g. after the config was swapped in. */
void ws_cfg_reset_state(struct ws_config *cfg);

/* Load order "current -> previous -> factory": the first source that reads
 * and compiles wins. `read` returns the length read or < 0. Returns the index
 * of the source used, or -1 if none (the factory set never fails in practice).
 * `errs` holds the errors of the last failed source. */
struct ws_cfg_source {
	const char *name;
	int (*read)(void *ctx, char *buf, size_t cap);
	void *ctx;
};

int ws_cfg_load_chain(const struct ws_cfg_source *srcs, int n, char *buf, size_t cap,
		      struct ws_config *out, struct ws_cfg_errors *errs, struct ws_jtok *toks,
		      int max_toks);

/* The factory set from docs/screen-constructor.md, 12 (embedded JSON). */
extern const unsigned char ws_factory_screens_json[];
extern const unsigned int ws_factory_screens_json_len;

#ifdef __cplusplus
}
#endif

#endif /* WS_CONFIG_H_ */
