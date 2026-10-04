/* REST handlers (spec section 10). All bodies and answers are JSON. */
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/http/method.h>
#include <zephyr/net/http/status.h>

#include <ws/ha.h>
#include <ws/json.h>
#include <ws/screens.h>
#include <ws/util.h>

#include "app.h"
#include "display/display.h"
#include "io/io.h"
#include "mqtt/mqtt_svc.h"
#include "ota/ota.h"
#include "web.h"
#ifdef CONFIG_WS_NET
#include "net/net_mgr.h"
#endif

extern const unsigned char ws_glyphs_json[];
extern const unsigned int ws_glyphs_json_len;

static struct ws_cfg_errors errs;
/* one token buffer for the web thread (requests are served one at a time) */
static struct ws_jtok toks[WS_JSON_TOKENS];

static void errors_resp(struct api_resp *r, int status)
{
	struct ws_jw w;

	ws_jw_init(&w, r->buf, r->cap);
	ws_jw_obj(&w);
	ws_jw_kbool(&w, "ok", false);
	ws_jw_key(&w, "errors");
	ws_cfg_errors_json(&errs, &w);
	ws_jw_obj_end(&w);
	api_json(r, status, &w);
}

static void ok_resp(struct api_resp *r)
{
	struct ws_jw w;

	ws_jw_init(&w, r->buf, r->cap);
	ws_jw_obj(&w);
	ws_jw_kbool(&w, "ok", true);
	ws_jw_obj_end(&w);
	api_json(r, HTTP_200_OK, &w);
}

void api_status(int m, char *b, size_t n, struct api_resp *r)
{
	struct ws_jw w;
	struct ws_tm tm;
	struct ws_msg_net net = {0};
	char s[48];

	zbus_chan_read(&ws_chan_net, &net, K_MSEC(10));
	ws_jw_init(&w, r->buf, r->cap);
	ws_jw_obj(&w);
	ws_jw_kstr(&w, "id", ws_app_device_id());
	ws_jw_kstr(&w, "version", CONFIG_WS_VERSION);
	ws_jw_kint(&w, "uptime", ws_app_now().mono);
#ifdef CONFIG_WS_NET
	ws_jw_kstr(&w, "net", ws_net_state_str());
#else
	ws_jw_kstr(&w, "net", "none");
#endif
	ws_jw_kstr(&w, "ip", net.ip);
	ws_jw_kint(&w, "rssi", net.rssi);
	ws_jw_kbool(&w, "mqtt", ws_mqtt_connected());
	ws_jw_key(&w, "time");
	if (ws_app_localtime(&tm)) {
		snprintf(s, sizeof(s), "%04d-%02d-%02d %02d:%02d:%02d", tm.year, tm.month, tm.day,
			 tm.hour, tm.min, tm.sec);
		ws_jw_str(&w, s);
	} else {
		ws_jw_null(&w);
	}
	ws_jw_kstr(&w, "ntp_server", ws_app_time_server());
	ws_jw_kint(&w, "ntp_last", ws_app_last_sync());
	ws_jw_kstr(&w, "screens_source", ws_app_cfg_source());
	ws_jw_kbool(&w, "lamp", ws_io_lamp_state());
	ws_jw_kstr(&w, "ota", ws_ota_state_str());
	ws_jw_obj_end(&w);
	api_json(r, HTTP_200_OK, &w);
}

void api_screens(int m, char *b, size_t n, struct api_resp *r)
{
	if (m == HTTP_GET) {
		int len = ws_app_cfg_json(r->buf, r->cap);

		if (len < 0) {
			api_error(r, HTTP_500_INTERNAL_SERVER_ERROR, "конфигурация не помещается");
			return;
		}
		r->status = HTTP_200_OK;
		r->len = len;
		return;
	}
	/* PUT: check, save, apply */
	if (ws_app_cfg_apply(b, n, true, &errs)) {
		errors_resp(r, HTTP_422_UNPROCESSABLE_ENTITY);
		return;
	}
	ok_resp(r);
}

void api_screens_validate(int m, char *b, size_t n, struct api_resp *r)
{
	static struct ws_config scratch;

	if (ws_cfg_compile(b, n, &scratch, &errs, toks, WS_JSON_TOKENS)) {
		errors_resp(r, HTTP_422_UNPROCESSABLE_ENTITY);
		return;
	}
	ok_resp(r);
}

void api_screens_rollback(int m, char *b, size_t n, struct api_resp *r)
{
	if (ws_app_cfg_rollback(&errs)) {
		errors_resp(r, HTTP_422_UNPROCESSABLE_ENTITY);
		return;
	}
	ok_resp(r);
}

void api_screens_factory(int m, char *b, size_t n, struct api_resp *r)
{
	if (ws_app_cfg_apply((const char *)ws_factory_screens_json, ws_factory_screens_json_len,
			     true, &errs)) {
		errors_resp(r, HTTP_500_INTERNAL_SERVER_ERROR);
		return;
	}
	ok_resp(r);
}

void api_catalog(int m, char *b, size_t n, struct api_resp *r)
{
	ws_app_lock();
	int len = ws_catalog_json(ws_app_cfg(), r->buf, r->cap);

	ws_app_unlock();
	if (len < 0) {
		api_error(r, HTTP_500_INTERNAL_SERVER_ERROR, "каталог не помещается");
		return;
	}
	r->status = HTTP_200_OK;
	r->len = len;
}

void api_glyphs(int m, char *b, size_t n, struct api_resp *r)
{
	/* built-in glyphs (generated from core.js) + the user pictograms */
	size_t base = ws_glyphs_json_len;

	while (base && ws_glyphs_json[base - 1] != '}') {
		base--;
	}
	if (base < 2 || base + 64 > r->cap) {
		api_error(r, HTTP_500_INTERNAL_SERVER_ERROR, "нет шрифтов");
		return;
	}
	memcpy(r->buf, ws_glyphs_json, base - 1);
	struct ws_jw w;

	ws_jw_init(&w, r->buf + base - 1, r->cap - base + 1);
	/* continue the object: ,"user_pictos":[...]} */
	ws_jw_raw(&w, ",\"user_pictos\":[", 16);
	ws_app_lock();
	struct ws_config *c = ws_app_cfg();

	for (int i = 0; i < c->n_pictos; i++) {
		char hex[8];

		if (i) {
			ws_jw_raw(&w, ",", 1);
		}
		w.first |= 1u << w.depth; /* raw comma written above */
		ws_jw_obj(&w);
		ws_jw_kstr(&w, "name", c->pictos[i].name);
		ws_jw_kint(&w, "size", c->pictos[i].bm.w);
		ws_jw_key(&w, "rows");
		ws_jw_arr(&w);
		for (int y = 0; y < c->pictos[i].bm.h; y++) {
			snprintf(hex, sizeof(hex), "%04x", c->pictos[i].bm.rows[y]);
			ws_jw_str(&w, hex);
		}
		ws_jw_arr_end(&w);
		ws_jw_obj_end(&w);
	}
	ws_app_unlock();
	ws_jw_raw(&w, "]}", 2);
	if (!w.ok) {
		api_error(r, HTTP_500_INTERNAL_SERVER_ERROR, "шрифты не помещаются");
		return;
	}
	r->status = HTTP_200_OK;
	r->len = base - 1 + w.pos;
}

void api_vars(int m, char *b, size_t n, struct api_resp *r)
{
	struct ws_now now = ws_app_now();

	ws_app_lock();
	int len = ws_vars_to_json(ws_app_vars(), &now, r->buf, r->cap);

	ws_app_unlock();
	if (len < 0) {
		api_error(r, HTTP_500_INTERNAL_SERVER_ERROR, "переменные не помещаются");
		return;
	}
	r->status = HTTP_200_OK;
	r->len = len;
}

void api_display_state(int m, char *b, size_t n, struct api_resp *r)
{
	struct ws_jw w;
	struct ws_display_stats st;

	ws_display_get_stats(&st);
	ws_jw_init(&w, r->buf, r->cap);
	ws_jw_obj(&w);
	ws_app_lock();
	struct ws_engine *e = ws_app_engine();
	struct ws_config *c = ws_app_cfg();

	ws_jw_kstr(&w, "screen", c->screens[e->current].id);
	ws_jw_kstr(&w, "name", c->screens[e->current].name);
	ws_jw_kstr(&w, "reason", e->reason);
	ws_jw_key(&w, "pinned");
	if (e->pinned >= 0) {
		ws_jw_str(&w, c->screens[e->pinned].id);
	} else {
		ws_jw_null(&w);
	}
	ws_jw_key(&w, "candidates");
	ws_jw_arr(&w);
	for (int s = 0; s < c->n_screens; s++) {
		if ((e->last_candidates >> s) & 1) {
			ws_jw_str(&w, c->screens[s].id);
		}
	}
	ws_jw_arr_end(&w);
	ws_app_unlock();
	ws_jw_kint(&w, "flips_24h", st.flips_24h);
	ws_jw_kint(&w, "frames", st.frames_sent);
	ws_jw_obj_end(&w);
	api_json(r, HTTP_200_OK, &w);
}

/* {"config": {...} (optional, else the active one), "screen": "id",
 *  "vars": {...} (overrides), "sod": seconds of day (optional)}
 * -> {"frame": "<base64 of 141 bytes>", "rows": ["#..", ...]} */
void api_render(int m, char *b, size_t n, struct api_resp *r)
{
	static struct ws_config cfg;
	static struct ws_vars vars;
	struct ws_json j;
	struct ws_frame f;
	struct ws_now now = ws_app_now();
	char id[WS_ID_LEN] = "";
	int32_t sod = -1;
	bool has_sod;
	size_t cfg_at = 0, cfg_len = 0, vars_at = 0, vars_len = 0;

	if (ws_json_parse(&j, b, n, toks, WS_JSON_TOKENS) < 0 || !ws_json_is(&j, 0, WS_J_OBJ)) {
		api_error(r, HTTP_400_BAD_REQUEST, "ожидается объект JSON");
		return;
	}
	/* remember the parts, the token buffer is reused below */
	int ct = ws_json_get(&j, 0, "config");
	int vt = ws_json_get(&j, 0, "vars");

	if (ct >= 0) {
		cfg_at = j.t[ct].start;
		cfg_len = j.t[ct].end - j.t[ct].start;
	}
	if (vt >= 0) {
		vars_at = j.t[vt].start;
		vars_len = j.t[vt].end - j.t[vt].start;
	}
	ws_json_str(&j, ws_json_get(&j, 0, "screen"), id, sizeof(id));
	has_sod = ws_json_int(&j, ws_json_get(&j, 0, "sod"), &sod);

	if (cfg_len) {
		if (ws_cfg_compile(b + cfg_at, cfg_len, &cfg, &errs, toks, WS_JSON_TOKENS)) {
			errors_resp(r, HTTP_422_UNPROCESSABLE_ENTITY);
			return;
		}
	} else {
		ws_app_lock();
		cfg = *ws_app_cfg();
		ws_app_unlock();
	}
	ws_app_lock();
	vars = *ws_app_vars();
	ws_app_unlock();
	if (vars_len && ws_json_parse(&j, b + vars_at, vars_len, toks, WS_JSON_TOKENS) > 0) {
		ws_vars_apply_json(&vars, &j, 0, now.mono);
	}
	int s = id[0] ? ws_cfg_screen_by_id(&cfg, id) : cfg.def;

	if (s < 0) {
		api_error(r, HTTP_404_NOT_FOUND, "нет такого экрана");
		return;
	}
	struct ws_tm tm;

	if (!has_sod) {
		sod = ws_app_localtime(&tm) ? tm.hour * 3600 + tm.min * 60 + tm.sec : -1;
	}
	ws_cfg_reset_state(&cfg);
	struct ws_render_ctx rc = {&cfg, &vars, now, sod};

	ws_render_screen(&rc, s, &f);

	struct ws_jw w;
	char b64[200], row[WS_W + 1];

	ws_base64_encode(f.bits, sizeof(f.bits), b64, sizeof(b64));
	ws_jw_init(&w, r->buf, r->cap);
	ws_jw_obj(&w);
	ws_jw_kstr(&w, "screen", cfg.screens[s].id);
	ws_jw_kstr(&w, "frame", b64);
	ws_jw_key(&w, "rows");
	ws_jw_arr(&w);
	for (int y = 0; y < WS_H; y++) {
		for (int x = 0; x < WS_W; x++) {
			row[x] = ws_frame_get(&f, x, y) ? '#' : '.';
		}
		row[WS_W] = '\0';
		ws_jw_str(&w, row);
	}
	ws_jw_arr_end(&w);
	ws_jw_obj_end(&w);
	api_json(r, HTTP_200_OK, &w);
}

/* {"config": {...}, "screen": "id", "seconds": 60} */
void api_preview(int m, char *b, size_t n, struct api_resp *r)
{
	struct ws_json j;
	char id[WS_ID_LEN] = "";
	int32_t seconds = 60;

	if (ws_json_parse(&j, b, n, toks, WS_JSON_TOKENS) < 0) {
		api_error(r, HTTP_400_BAD_REQUEST, "ожидается объект JSON");
		return;
	}
	int ct = ws_json_get(&j, 0, "config");

	ws_json_str(&j, ws_json_get(&j, 0, "screen"), id, sizeof(id));
	ws_json_int(&j, ws_json_get(&j, 0, "seconds"), &seconds);
	if (ct < 0) {
		api_error(r, HTTP_400_BAD_REQUEST, "нужен «config»");
		return;
	}
	size_t at = j.t[ct].start, len = j.t[ct].end - j.t[ct].start;

	if (ws_display_preview(b + at, len, id[0] ? id : NULL, seconds > 300 ? 300 : seconds, toks,
			       &errs)) {
		errors_resp(r, HTTP_422_UNPROCESSABLE_ENTITY);
		return;
	}
	ok_resp(r);
}

/* {"screen": "id", "minutes": 30} or {"screen": null} */
void api_pin(int m, char *b, size_t n, struct api_resp *r)
{
	int screen;
	uint32_t minutes;

	ws_app_lock();
	int res = ws_parse_pin(ws_app_cfg(), b, n, &screen, &minutes);

	ws_app_unlock();
	if (res) {
		api_error(r, HTTP_400_BAD_REQUEST, "нет такого экрана");
		return;
	}
	ws_display_pin(screen, minutes);
	ok_resp(r);
}

void api_settings(int m, char *b, size_t n, struct api_resp *r)
{
	if (m == HTTP_POST) {
		if (ws_app_settings_apply(b, n, &errs)) {
			errors_resp(r, HTTP_422_UNPROCESSABLE_ENTITY);
			return;
		}
	}
	struct ws_settings s;

	ws_app_settings_get(&s);
	int len = ws_settings_to_json(&s, r->buf, r->cap);

	r->status = len < 0 ? HTTP_500_INTERNAL_SERVER_ERROR : HTTP_200_OK;
	r->len = len < 0 ? 0 : len;
}

void api_wifi_scan(int m, char *b, size_t n, struct api_resp *r)
{
	struct ws_jw w;

	ws_jw_init(&w, r->buf, r->cap);
	ws_jw_obj(&w);
	ws_jw_key(&w, "networks");
	ws_jw_arr(&w);
#ifdef CONFIG_WS_NET
	struct ws_wifi_net nets[WS_WIFI_SCAN_MAX];
	int cnt = ws_net_scan_results(nets, WS_WIFI_SCAN_MAX);

	for (int i = 0; i < cnt; i++) {
		ws_jw_obj(&w);
		ws_jw_kstr(&w, "ssid", nets[i].ssid);
		ws_jw_kint(&w, "rssi", nets[i].rssi);
		ws_jw_kbool(&w, "open", nets[i].open);
		ws_jw_obj_end(&w);
	}
#endif
	ws_jw_arr_end(&w);
	ws_jw_obj_end(&w);
	api_json(r, HTTP_200_OK, &w);
}

/* {"on": true} */
void api_lamp(int m, char *b, size_t n, struct api_resp *r)
{
	struct ws_jtok toks[8];
	struct ws_json j;
	bool on;

	if (ws_json_parse(&j, b, n, toks, 8) < 0 ||
	    !ws_json_bool(&j, ws_json_get(&j, 0, "on"), &on)) {
		api_error(r, HTTP_400_BAD_REQUEST, "ожидается {\"on\": true|false}");
		return;
	}
	struct ws_msg_lamp msg = {on};

	zbus_chan_pub(&ws_chan_lamp_cmd, &msg, K_MSEC(100));
	ok_resp(r);
}
