/* Device settings in NVS (settings subsystem, keys "ws/<field>"). */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>

#include <ws/util.h>

#include "app.h"
#include "storage.h"

LOG_MODULE_REGISTER(ws_settings, LOG_LEVEL_INF);

static struct ws_settings cur;
static bool loaded;
K_MUTEX_DEFINE(set_lock);

static int h_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const struct ws_setting_field *f = ws_setting_find(key);

	if (!f) {
		return -ENOENT;
	}
	void *p = ws_setting_ptr(&cur, f);

	if (f->type == WS_SET_STR) {
		char *s = p;
		ssize_t n = read_cb(cb_arg, s, f->size - 1);

		s[n > 0 ? n : 0] = '\0'; /* stored with or without the terminator */
	} else if (len == sizeof(int32_t)) {
		read_cb(cb_arg, p, sizeof(int32_t));
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(ws, "ws", NULL, h_set, NULL, NULL);

int ws_settings_init(void)
{
	int r;

	ws_settings_defaults(&cur);
	r = settings_subsys_init();
	if (r) {
		LOG_ERR("settings init: %d", r);
		return r;
	}
	r = settings_load_subtree("ws");
	loaded = true;
	LOG_INF("settings loaded (%d), wifi '%s', mqtt '%s'", r, cur.wifi_ssid, cur.mqtt_host);
	return 0;
}

void ws_app_settings_get(struct ws_settings *out)
{
	k_mutex_lock(&set_lock, K_FOREVER);
	if (!loaded) {
		ws_settings_defaults(&cur);
	}
	*out = cur;
	k_mutex_unlock(&set_lock);
}

static void store(uint64_t changed)
{
	char key[48];

	for (size_t i = 0; i < ws_setting_field_count && i < 64; i++) {
		if (!(changed & (1ULL << i))) {
			continue;
		}
		const struct ws_setting_field *f = &ws_setting_fields[i];
		const void *p = ws_setting_ptr(&cur, f);
		size_t len = f->type == WS_SET_STR ? strlen(p) + 1 : sizeof(int32_t);

		snprintf(key, sizeof(key), "ws/%s", f->key);
		int r = settings_save_one(key, p, len);

		if (r) {
			LOG_ERR("save %s: %d", key, r);
		}
	}
}

int ws_app_settings_apply(const char *json, size_t len, struct ws_cfg_errors *errs)
{
	uint8_t rnd[8];
	char salt[17];
	uint64_t changed;
	int r;

	sys_rand_get(rnd, sizeof(rnd));
	ws_hex(rnd, sizeof(rnd), salt);
	k_mutex_lock(&set_lock, K_FOREVER);
	r = ws_settings_apply_json(&cur, json, len, salt, &changed, errs);
	if (r == 0) {
		store(changed);
	}
	k_mutex_unlock(&set_lock);
	if (r == 0 && changed) {
		struct ws_msg_settings m = {changed};

		zbus_chan_pub(&ws_chan_settings, &m, K_MSEC(100));
	}
	return r;
}

void ws_app_settings_set_int(const char *key, int32_t v)
{
	const struct ws_setting_field *f = ws_setting_find(key);

	if (!f || f->type == WS_SET_STR) {
		return;
	}
	k_mutex_lock(&set_lock, K_FOREVER);
	int32_t *p = ws_setting_ptr(&cur, f);

	if (*p != v) {
		*p = v;
		store(1ULL << (f - ws_setting_fields));
	}
	k_mutex_unlock(&set_lock);
}
