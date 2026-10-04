/* Device settings table, see ws/settings.h. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <ws/json.h>
#include <ws/settings.h>
#include <ws/tz.h>
#include <ws/util.h>

static const char *const lamp_modes[] = {"off", "on", "last", NULL};

#define STR(k, field, secret, reboot)                                                              \
	{                                                                                          \
		k, WS_SET_STR, secret, reboot, offsetof(struct ws_settings, field),                \
			sizeof(((struct ws_settings *)0)->field), 0, 0, NULL                       \
	}
#define INT(k, field, lo, hi, reboot)                                                              \
	{                                                                                          \
		k, WS_SET_INT, false, reboot, offsetof(struct ws_settings, field),                 \
			sizeof(int32_t), lo, hi, NULL                                              \
	}

const struct ws_setting_field ws_setting_fields[] = {
	STR("wifi.ssid", wifi_ssid, false, true),
	STR("wifi.psk", wifi_psk, true, true),
	STR("mqtt.host", mqtt_host, false, true),
	INT("mqtt.port", mqtt_port, 1, 65535, true),
	STR("mqtt.user", mqtt_user, false, true),
	STR("mqtt.pass", mqtt_pass, true, true),
	STR("ntp.server1", ntp1, false, false),
	STR("ntp.server2", ntp2, false, false),
	INT("ntp.interval", ntp_interval, 5, 1440, false),
	STR("ntp.tz", tz, false, false),
	STR("metar.icao", metar_icao, false, false),
	STR("metar.url1", metar_url1, false, false),
	STR("metar.url2", metar_url2, false, false),
	INT("metar.period", metar_period, 10, 180, false),
	INT("geo.lat", lat_e6, -90000000, 90000000, false),
	INT("geo.lon", lon_e6, -180000000, 180000000, false),
	STR("web.user", web_user, false, false),
	STR("web.hash", web_hash, true, false),
	STR("web.salt", web_salt, true, false),
	{"lamp.restore", WS_SET_ENUM, false, false, offsetof(struct ws_settings, lamp_restore),
	 sizeof(int32_t), 0, 2, lamp_modes},
	INT("lamp.last", lamp_last, 0, 1, false),
	INT("sensor.t_offset", t_offset, -100, 100, false),
	INT("sensor.rh_offset", rh_offset, -200, 200, false),
	STR("dev.id", dev_id, false, true),
	STR("metar.ca", ca_pem, false, false),
};

const size_t ws_setting_field_count = sizeof(ws_setting_fields) / sizeof(ws_setting_fields[0]);

void ws_settings_defaults(struct ws_settings *s)
{
	memset(s, 0, sizeof(*s));
	s->mqtt_port = 1883;
	s->ntp_interval = 60;
	strcpy(s->tz, "MSK-3");
	strcpy(s->metar_url1,
	       "https://tgftp.nws.noaa.gov/data/observations/metar/stations/{icao}.TXT");
	strcpy(s->metar_url2, "https://aviationweather.gov/api/data/metar?ids={icao}&format=raw");
	s->metar_period = 30;
	strcpy(s->web_user, "admin");
	s->lamp_restore = WS_LAMP_RESTORE_LAST;
}

const struct ws_setting_field *ws_setting_find(const char *key)
{
	for (size_t i = 0; i < ws_setting_field_count; i++) {
		if (strcmp(ws_setting_fields[i].key, key) == 0) {
			return &ws_setting_fields[i];
		}
	}
	return NULL;
}

void *ws_setting_ptr(struct ws_settings *s, const struct ws_setting_field *f)
{
	return (uint8_t *)s + f->offset;
}

int ws_settings_to_json(const struct ws_settings *s, char *buf, size_t len)
{
	struct ws_jw w;
	char k[40];

	ws_jw_init(&w, buf, len);
	ws_jw_obj(&w);
	for (size_t i = 0; i < ws_setting_field_count; i++) {
		const struct ws_setting_field *f = &ws_setting_fields[i];
		const void *p = (const uint8_t *)s + f->offset;

		if (f->secret) {
			if (!strcmp(f->key, "web.salt")) {
				continue;
			}
			snprintf(k, sizeof(k), "%s_set", f->key);
			ws_jw_kbool(&w, k, ((const char *)p)[0] != '\0');
			continue;
		}
		switch (f->type) {
		case WS_SET_STR:
			ws_jw_kstr(&w, f->key, p);
			break;
		case WS_SET_INT:
			ws_jw_kint(&w, f->key, *(const int32_t *)p);
			break;
		case WS_SET_ENUM:
			ws_jw_kstr(&w, f->key, f->enums[*(const int32_t *)p]);
			break;
		}
	}
	ws_jw_obj_end(&w);
	return w.ok ? (int)w.pos : -1;
}

static void err(struct ws_cfg_errors *e, const char *path, const char *msg)
{
	if (e->count < WS_MAX_ERRORS) {
		snprintf(e->e[e->count].path, sizeof(e->e[0].path), "%s", path);
		snprintf(e->e[e->count].msg, sizeof(e->e[0].msg), "%s", msg);
	}
	e->count++;
}

static bool valid_icao(const char *s)
{
	if (!s[0]) {
		return true;
	}
	if (strlen(s) != 4) {
		return false;
	}
	for (int i = 0; i < 4; i++) {
		if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9'))) {
			return false;
		}
	}
	return true;
}

int ws_settings_apply_json(struct ws_settings *s, const char *json, size_t len,
			   const char *new_salt, uint64_t *changed, struct ws_cfg_errors *errs)
{
	static struct ws_settings tmp;
	struct ws_jtok toks[128];
	struct ws_json j;
	static char sv[2048];

	memset(errs, 0, sizeof(*errs));
	*changed = 0;
	if (ws_json_parse(&j, json, len, toks, 128) < 0 || !ws_json_is(&j, 0, WS_J_OBJ)) {
		err(errs, "", "ожидается объект JSON");
		return -1;
	}
	tmp = *s;
	for (int k = ws_json_first(&j, 0); k >= 0; k = ws_json_next(&j, 0, k)) {
		char key[40];
		int v = k + 1;

		if (ws_json_str(&j, k, key, sizeof(key)) < 0) {
			err(errs, "", "слишком длинный ключ");
			continue;
		}
		if (!strcmp(key, "web.password")) {
			if (ws_json_str(&j, v, sv, sizeof(sv)) < 4 || strlen(sv) > 64) {
				err(errs, key, "пароль от 4 до 64 символов");
				continue;
			}
			ws_settings_set_password(&tmp, sv, new_salt ? new_salt : "");
			continue;
		}
		const struct ws_setting_field *f = ws_setting_find(key);

		if (!f || !strcmp(key, "web.hash") || !strcmp(key, "web.salt")) {
			err(errs, key, "неизвестная настройка");
			continue;
		}
		void *p = ws_setting_ptr(&tmp, f);

		switch (f->type) {
		case WS_SET_STR: {
			int n = ws_json_str(&j, v, sv, sizeof(sv));

			if (n < 0 || (size_t)n >= f->size) {
				err(errs, key, "строка слишком длинная или не строка");
				continue;
			}
			strcpy(p, sv);
			break;
		}
		case WS_SET_INT: {
			int32_t x;

			if (!ws_json_int(&j, v, &x) || x < f->min || x > f->max) {
				char m[64];

				snprintf(m, sizeof(m), "целое от %ld до %ld", (long)f->min,
					 (long)f->max);
				err(errs, key, m);
				continue;
			}
			*(int32_t *)p = x;
			break;
		}
		case WS_SET_ENUM: {
			int found = -1;

			if (ws_json_str(&j, v, sv, sizeof(sv)) > 0) {
				for (int e = 0; f->enums[e]; e++) {
					if (!strcmp(f->enums[e], sv)) {
						found = e;
					}
				}
			}
			if (found < 0) {
				err(errs, key, "недопустимое значение");
				continue;
			}
			*(int32_t *)p = found;
			break;
		}
		}
	}
	/* cross-field checks */
	struct ws_tz tz;

	if (ws_tz_parse(tmp.tz, &tz) < 0) {
		err(errs, "ntp.tz",
		    "строка POSIX TZ, например MSK-3 или CET-1CEST,M3.5.0,M10.5.0/3");
	}
	if (!valid_icao(tmp.metar_icao)) {
		err(errs, "metar.icao", "четыре заглавные буквы, например UWGG");
	}
	if (tmp.metar_url1[0] && strncmp(tmp.metar_url1, "https://", 8) != 0) {
		err(errs, "metar.url1", "нужен адрес https://");
	}
	if (tmp.metar_url2[0] && strncmp(tmp.metar_url2, "https://", 8) != 0) {
		err(errs, "metar.url2", "нужен адрес https://");
	}
	for (const char *c = tmp.dev_id; *c; c++) {
		if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) {
			err(errs, "dev.id", "строчная латиница, цифры и _");
			break;
		}
	}
	if (errs->count) {
		return -1;
	}
	for (size_t i = 0; i < ws_setting_field_count && i < 64; i++) {
		const struct ws_setting_field *f = &ws_setting_fields[i];

		if (memcmp((uint8_t *)s + f->offset, (uint8_t *)&tmp + f->offset, f->size) != 0) {
			*changed |= 1ULL << i;
		}
	}
	*s = tmp;
	return 0;
}

static void hash_pw(const char *salt, const char *pw, char out[65])
{
	struct ws_sha256 h;
	uint8_t d[32];

	ws_sha256_init(&h);
	ws_sha256_update(&h, salt, strlen(salt));
	ws_sha256_update(&h, pw, strlen(pw));
	ws_sha256_final(&h, d);
	ws_hex(d, 32, out);
}

void ws_settings_set_password(struct ws_settings *s, const char *password, const char *salt)
{
	snprintf(s->web_salt, sizeof(s->web_salt), "%s", salt);
	hash_pw(s->web_salt, password, s->web_hash);
}

bool ws_settings_check_password(const struct ws_settings *s, const char *password)
{
	char h[65];

	if (!s->web_hash[0]) {
		return false;
	}
	hash_pw(s->web_salt, password, h);
	return ws_ct_equal(h, s->web_hash, 64);
}

int ws_settings_metar_url(const struct ws_settings *s, int which, char *buf, size_t len)
{
	const char *t = which == 0 ? s->metar_url1 : s->metar_url2;
	const char *p = strstr(t, "{icao}");

	if (!t[0]) {
		return -1;
	}
	if (!p) {
		return snprintf(buf, len, "%s", t);
	}
	return snprintf(buf, len, "%.*s%s%s", (int)(p - t), t, s->metar_icao, p + 6);
}
