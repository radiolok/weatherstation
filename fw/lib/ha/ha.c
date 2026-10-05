/* MQTT payloads and Home Assistant discovery, see ws/ha.h. */
#include <stdio.h>
#include <string.h>

#include <ws/ha.h>
#include <ws/json.h>

int ws_topic(char *buf, size_t len, const char *id, const char *sub)
{
	return snprintf(buf, len, "ws/%s/%s", id, sub);
}

static void device_block(struct ws_jw *w, const struct ws_ha_device *dev)
{
	ws_jw_key(w, "device");
	ws_jw_obj(w);
	ws_jw_key(w, "identifiers");
	ws_jw_arr(w);
	ws_jw_str(w, dev->id);
	ws_jw_arr_end(w);
	ws_jw_kstr(w, "name",
		   "\xD0\x9C\xD0\xB5\xD1\x82\xD0\xB5\xD0\xBE\xD1\x81\xD1\x82\xD0\xB0\xD0"
		   "\xBD\xD1\x86\xD0\xB8\xD1\x8F"); /* Метеостанция */
	ws_jw_kstr(w, "manufacturer", "radiolok");
	ws_jw_kstr(w, "model", "Mobitec 102x11 weatherstation");
	ws_jw_kstr(w, "sw_version", dev->version);
	ws_jw_obj_end(w);
}

static void common(struct ws_jw *w, const struct ws_ha_device *dev, const char *name,
		   const char *obj)
{
	char s[96];

	ws_jw_kstr(w, "name", name);
	snprintf(s, sizeof(s), "%s_%s", dev->id, obj);
	ws_jw_kstr(w, "unique_id", s);
	snprintf(s, sizeof(s), "%s_%s", dev->id, obj);
	ws_jw_kstr(w, "object_id", s);
	ws_topic(s, sizeof(s), dev->id, "status");
	ws_jw_kstr(w, "availability_topic", s);
	device_block(w, dev);
}

struct sensor_def {
	const char *obj, *name, *field, *unit, *dev_class;
	bool diag;
};

static const struct sensor_def sensors[] = {
	[WS_HA_TEMP] = {"t",
			"\xD0\xA2\xD0\xB5\xD0\xBC\xD0\xBF\xD0\xB5\xD1\x80\xD0\xB0\xD1\x82\xD1\x83"
			"\xD1\x80\xD0\xB0", /* Температура */
			"t",
			"\xC2\xB0"
			"C",
			"temperature", false},
	[WS_HA_HUMIDITY] = {"rh",
			    "\xD0\x92\xD0\xBB\xD0\xB0\xD0\xB6\xD0\xBD\xD0\xBE\xD1\x81\xD1\x82"
			    "\xD1\x8C", /* Влажность */
			    "rh", "%", "humidity", false},
	[WS_HA_PRESSURE] = {"p", "\xD0\x94\xD0\xB0\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5",
			    /* Давление */ "p", "mmHg", "pressure", false},
	[WS_HA_CO2] = {"co2", "CO2", "co2", "ppm", "carbon_dioxide", false},
	[WS_HA_RSSI] = {"rssi", "Wi-Fi RSSI", "rssi", "dBm", "signal_strength", true},
};

int ws_ha_discovery(const struct ws_ha_device *dev, enum ws_ha_entity e,
		    const struct ws_config *cfg, char *topic, size_t topic_len, char *payload,
		    size_t payload_len)
{
	struct ws_jw w;
	char s[96], tpl[64];

	ws_jw_init(&w, payload, payload_len);
	ws_jw_obj(&w);
	switch (e) {
	case WS_HA_TEMP:
	case WS_HA_HUMIDITY:
	case WS_HA_PRESSURE:
	case WS_HA_CO2:
	case WS_HA_RSSI: {
		const struct sensor_def *d = &sensors[e];

		snprintf(topic, topic_len, WS_HA_PREFIX "/sensor/%s/%s/config", dev->id, d->obj);
		common(&w, dev, d->name, d->obj);
		ws_topic(s, sizeof(s), dev->id, "sensors");
		ws_jw_kstr(&w, "state_topic", s);
		snprintf(tpl, sizeof(tpl), "{{ value_json.%s }}", d->field);
		ws_jw_kstr(&w, "value_template", tpl);
		ws_jw_kstr(&w, "unit_of_measurement", d->unit);
		ws_jw_kstr(&w, "device_class", d->dev_class);
		ws_jw_kstr(&w, "state_class", "measurement");
		if (d->diag) {
			ws_jw_kstr(&w, "entity_category", "diagnostic");
		}
		break;
	}
	case WS_HA_LAMP:
		snprintf(topic, topic_len, WS_HA_PREFIX "/light/%s/lamp/config", dev->id);
		common(&w, dev,
		       "\xD0\x9F\xD0\xBE\xD0\xB4\xD1\x81\xD0\xB2\xD0\xB5\xD1\x82\xD0\xBA\xD0\xB0 "
		       "\xD1\x82\xD0\xB0\xD0\xB1\xD0\xBB\xD0\xBE", /* Подсветка табло */
		       "lamp");
		ws_topic(s, sizeof(s), dev->id, "lamp/set");
		ws_jw_kstr(&w, "command_topic", s);
		ws_topic(s, sizeof(s), dev->id, "lamp/state");
		ws_jw_kstr(&w, "state_topic", s);
		ws_jw_kstr(&w, "payload_on", "ON");
		ws_jw_kstr(&w, "payload_off", "OFF");
		break;
	case WS_HA_SCREEN:
		snprintf(topic, topic_len, WS_HA_PREFIX "/select/%s/screen/config", dev->id);
		common(&w, dev,
		       "\xD0\xAD\xD0\xBA\xD1\x80\xD0\xB0\xD0\xBD \xD1\x82\xD0\xB0\xD0\xB1\xD0\xBB"
		       "\xD0\xBE", /* Экран табло */
		       "screen");
		ws_topic(s, sizeof(s), dev->id, "display/pin");
		ws_jw_kstr(&w, "command_topic", s);
		ws_topic(s, sizeof(s), dev->id, "display/state");
		ws_jw_kstr(&w, "state_topic", s);
		ws_jw_kstr(&w, "value_template",
			   "{{ value_json.name if value_json.pinned else '" WS_AUTO_RU "' }}");
		ws_jw_key(&w, "options");
		ws_jw_arr(&w);
		ws_jw_str(&w, WS_AUTO_RU);
		for (int i = 0; cfg && i < cfg->n_screens; i++) {
			ws_jw_str(&w, cfg->screens[i].name);
		}
		ws_jw_arr_end(&w);
		break;
	case WS_HA_REASON:
		snprintf(topic, topic_len, WS_HA_PREFIX "/sensor/%s/reason/config", dev->id);
		common(&w, dev,
		       "\xD0\x9F\xD1\x80\xD0\xB8\xD1\x87\xD0\xB8\xD0\xBD\xD0\xB0 \xD0\xB2\xD1\x8B"
		       "\xD0\xB1\xD0\xBE\xD1\x80\xD0\xB0 \xD1\x8D\xD0\xBA\xD1\x80\xD0\xB0\xD0\xBD"
		       "\xD0\xB0", /* Причина выбора экрана */
		       "reason");
		ws_topic(s, sizeof(s), dev->id, "display/state");
		ws_jw_kstr(&w, "state_topic", s);
		ws_jw_kstr(&w, "value_template", "{{ value_json.reason }}");
		ws_jw_kstr(&w, "entity_category", "diagnostic");
		break;
	default:
		return -1;
	}
	ws_jw_obj_end(&w);
	return w.ok ? (int)w.pos : -1;
}

static void opt_deci(struct ws_jw *w, const char *k, bool ok, int32_t v)
{
	ws_jw_key(w, k);
	if (ok) {
		ws_jw_deci(w, v);
	} else {
		ws_jw_null(w);
	}
}

static void opt_int(struct ws_jw *w, const char *k, bool ok, int32_t v)
{
	ws_jw_key(w, k);
	if (ok) {
		ws_jw_int(w, v);
	} else {
		ws_jw_null(w);
	}
}

int ws_sensors_json(const struct ws_sensor_report *r, char *buf, size_t len)
{
	struct ws_jw w;

	ws_jw_init(&w, buf, len);
	ws_jw_obj(&w);
	opt_deci(&w, "t", r->t_ok, r->t);
	opt_deci(&w, "rh", r->rh_ok, r->rh);
	opt_deci(&w, "p", r->p_ok, r->p);
	opt_deci(&w, "ptrend", r->trend_ok, r->trend);
	opt_int(&w, "co2", r->co2_ok, r->co2);
	opt_int(&w, "rssi", r->rssi_ok, r->rssi);
	ws_jw_obj_end(&w);
	return w.ok ? (int)w.pos : -1;
}

int ws_display_state_json(const char *id, const char *name, const char *reason, bool pinned,
			  uint32_t flips_24h, char *buf, size_t len)
{
	struct ws_jw w;

	ws_jw_init(&w, buf, len);
	ws_jw_obj(&w);
	ws_jw_kstr(&w, "screen", id);
	ws_jw_kstr(&w, "name", name);
	ws_jw_kstr(&w, "reason", reason);
	ws_jw_kbool(&w, "pinned", pinned);
	ws_jw_kint(&w, "flips_24h", flips_24h);
	ws_jw_obj_end(&w);
	return w.ok ? (int)w.pos : -1;
}

static bool eq_ci(const char *p, size_t len, const char *s)
{
	if (strlen(s) != len) {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		char a = p[i], b = s[i];

		if (a >= 'A' && a <= 'Z') {
			a = (char)(a - 'A' + 'a');
		}
		if (a != b) {
			return false;
		}
	}
	return true;
}

int ws_parse_onoff(const char *p, size_t len)
{
	while (len && (p[len - 1] == '\n' || p[len - 1] == ' ' || p[len - 1] == '\r')) {
		len--;
	}
	if (eq_ci(p, len, "on") || eq_ci(p, len, "1") || eq_ci(p, len, "true")) {
		return 1;
	}
	if (eq_ci(p, len, "off") || eq_ci(p, len, "0") || eq_ci(p, len, "false")) {
		return 0;
	}
	return -1;
}

static int find_screen(const struct ws_config *cfg, const char *s)
{
	for (int i = 0; i < cfg->n_screens; i++) {
		if (strcmp(cfg->screens[i].id, s) == 0 || strcmp(cfg->screens[i].name, s) == 0) {
			return i;
		}
	}
	return -2;
}

int ws_parse_pin(const struct ws_config *cfg, const char *p, size_t len, int *screen,
		 uint32_t *minutes)
{
	struct ws_jtok toks[16];
	struct ws_json j;
	char s[WS_NAME_LEN];

	*screen = -1;
	*minutes = 0;
	if (eq_ci(p, len, "auto") || (len == strlen(WS_AUTO_RU) && !memcmp(p, WS_AUTO_RU, len))) {
		return 0;
	}
	if (ws_json_parse(&j, p, len, toks, 16) > 0) {
		if (ws_json_is(&j, 0, WS_J_STR)) {
			if (ws_json_str(&j, 0, s, sizeof(s)) < 0) {
				return -1;
			}
			if (!strcmp(s, "auto") || !strcmp(s, WS_AUTO_RU)) {
				return 0;
			}
			*screen = find_screen(cfg, s);
			return *screen >= 0 ? 0 : -1;
		}
		if (!ws_json_is(&j, 0, WS_J_OBJ)) {
			return -1;
		}
		int st = ws_json_get(&j, 0, "screen");

		if (st < 0 || ws_json_is(&j, st, WS_J_NULL)) {
			return 0;
		}
		if (ws_json_str(&j, st, s, sizeof(s)) < 0) {
			return -1;
		}
		*screen = find_screen(cfg, s);
		int32_t m = 0;
		int mt = ws_json_get(&j, 0, "minutes");

		if (mt >= 0 && (!ws_json_int(&j, mt, &m) || m < 0 || m > 7 * 24 * 60)) {
			return -1;
		}
		*minutes = (uint32_t)m;
		return *screen >= 0 ? 0 : -1;
	}
	/* bare id or name */
	if (len >= sizeof(s)) {
		return -1;
	}
	memcpy(s, p, len);
	s[len] = '\0';
	*screen = find_screen(cfg, s);
	return *screen >= 0 ? 0 : -1;
}
