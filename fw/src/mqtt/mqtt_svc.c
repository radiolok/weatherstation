/*
 * MQTT and Home Assistant (F7, fw/README.md "MQTT и Home Assistant").
 *
 * One thread owns the client: it connects with a last will "offline",
 * publishes discovery, status, sensors, lamp and display state, and handles
 * lamp/set, forecast, display/pin, display/cfg/set, ota and the ext.*
 * topics. Other modules only raise flags; the thread publishes.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

#include <ws/ha.h>

#include "app.h"
#include "display/display.h"
#include "io/io.h"
#include "mqtt_svc.h"
#include "net/net_mgr.h"
#include "ota/ota.h"

LOG_MODULE_REGISTER(ws_mqtt, LOG_LEVEL_INF);

#define SENSORS_PERIOD_MS 30000
#define KEEPALIVE_S       60
#define BACKOFF_MAX_S     60
#define SMALL_PAYLOAD     2048

/* flags raised by other threads */
#define F_DISPLAY   BIT(0)
#define F_LAMP      BIT(1)
#define F_CFG       BIT(2) /* republish discovery and the config backup */
#define F_RECONNECT BIT(3)
#define F_SENSORS   BIT(4)

static atomic_t flags;
static struct mqtt_client client;
static struct sockaddr_storage broker;
static uint8_t rx_buf[512], tx_buf[1024];
static char payload[SMALL_PAYLOAD];
static char topic[128];
static char lwt_topic[64];
static struct mqtt_topic will_topic;
static struct mqtt_utf8 will_msg = {(const uint8_t *)"offline", 7};
static struct mqtt_utf8 user, pass;
static struct ws_settings set;
static bool connected;
static uint16_t msg_id = 1;
static K_SEM_DEFINE(wake, 0, 1);

/* sub-topics we subscribe to under ws/<id>/ */
static const char *const subs[] = {"lamp/set", "forecast", "display/pin", "display/cfg/set", "ota"};

static void raise(atomic_val_t f)
{
	atomic_or(&flags, f);
	k_sem_give(&wake);
}

void ws_mqtt_request_sensors(void)
{
	raise(F_SENSORS);
}

bool ws_mqtt_connected(void)
{
	return connected;
}

static void on_display(const struct zbus_channel *chan)
{
	raise(F_DISPLAY);
}
ZBUS_LISTENER_DEFINE(ws_mqtt_disp_lis, on_display);
ZBUS_CHAN_ADD_OBS(ws_chan_display, ws_mqtt_disp_lis, 5);

static void on_lamp(const struct zbus_channel *chan)
{
	raise(F_LAMP);
}
ZBUS_LISTENER_DEFINE(ws_mqtt_lamp_lis, on_lamp);
ZBUS_CHAN_ADD_OBS(ws_chan_lamp_state, ws_mqtt_lamp_lis, 5);

static void on_cfg(const struct zbus_channel *chan)
{
	raise(F_CFG);
}
ZBUS_LISTENER_DEFINE(ws_mqtt_cfg_lis, on_cfg);
ZBUS_CHAN_ADD_OBS(ws_chan_cfg, ws_mqtt_cfg_lis, 5);

static void on_settings(const struct zbus_channel *chan)
{
	const struct ws_msg_settings *m = zbus_chan_const_msg(chan);
	const struct ws_setting_field *h = ws_setting_find("mqtt.host");
	uint64_t mqtt_bits = 0;

	/* mqtt.host, mqtt.port, mqtt.user, mqtt.pass follow each other */
	for (int i = 0; i < 4; i++) {
		mqtt_bits |= 1ULL << ((h - ws_setting_fields) + i);
	}
	if (m->changed & mqtt_bits) {
		raise(F_RECONNECT);
	}
}
ZBUS_LISTENER_DEFINE(ws_mqtt_set_lis, on_settings);
ZBUS_CHAN_ADD_OBS(ws_chan_settings, ws_mqtt_set_lis, 5);

static int publish(const char *t, const void *data, size_t len, bool retain)
{
	struct mqtt_publish_param p = {0};

	if (!connected) {
		return -ENOTCONN;
	}
	p.message.topic.topic.utf8 = (const uint8_t *)t;
	p.message.topic.topic.size = strlen(t);
	p.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	p.message.payload.data = (uint8_t *)data;
	p.message.payload.len = len;
	p.message_id = msg_id++;
	p.retain_flag = retain;
	int r = mqtt_publish(&client, &p);

	if (r) {
		LOG_WRN("publish %s: %d", t, r);
	}
	return r;
}

static int publish_sub(const char *sub, const char *data, bool retain)
{
	ws_topic(topic, sizeof(topic), ws_app_device_id(), sub);
	return publish(topic, data, strlen(data), retain);
}

static void publish_discovery(void)
{
	struct ws_ha_device dev = {ws_app_device_id(), CONFIG_WS_VERSION};
	static char disc[1536];
	static char dtopic[128];

	for (int e = 0; e < WS_HA_ENTITY_COUNT; e++) {
		ws_app_lock();
		int n = ws_ha_discovery(&dev, e, ws_app_cfg(), dtopic, sizeof(dtopic), disc,
					sizeof(disc));
		ws_app_unlock();
		if (n > 0) {
			publish(dtopic, disc, n, true);
		}
	}
}

static void publish_cfg_backup(void)
{
	char *buf = ws_app_json_buf_take(K_SECONDS(2));

	if (!buf) {
		return;
	}
	int n = ws_app_cfg_json(buf, WS_MAX_FILE);

	if (n > 0) {
		ws_topic(topic, sizeof(topic), ws_app_device_id(), "display/cfg");
		publish(topic, buf, n, true);
	}
	ws_app_json_buf_give();
}

static void publish_display(void)
{
	struct ws_msg_display m;

	if (zbus_chan_read(&ws_chan_display, &m, K_MSEC(50)) || !m.screen[0]) {
		return;
	}
	if (ws_display_state_json(m.screen, m.name, m.reason, m.pinned, m.flips_24h, payload,
				  sizeof(payload)) > 0) {
		publish_sub("display/state", payload, true);
	}
}

static void publish_lamp(void)
{
	publish_sub("lamp/state", ws_io_lamp_state() ? "ON" : "OFF", true);
}

static void publish_sensors(void)
{
	struct ws_sensor_report r = {0};
	struct ws_now now = ws_app_now();
	struct ws_value v;

	ws_app_lock();
	struct ws_vars *vars = ws_app_vars();

	if ((r.t_ok = ws_vars_get(vars, WS_V_IN_T, &now, &v))) {
		r.t = v.num;
	}
	if ((r.rh_ok = ws_vars_get(vars, WS_V_IN_RH, &now, &v))) {
		r.rh = v.num;
	}
	if ((r.p_ok = ws_vars_get(vars, WS_V_IN_P, &now, &v))) {
		r.p = v.num;
	}
	if ((r.trend_ok = ws_vars_get(vars, WS_V_IN_P_TREND, &now, &v))) {
		r.trend = v.num;
	}
	if ((r.co2_ok = ws_vars_get(vars, WS_V_IN_CO2, &now, &v))) {
		r.co2 = v.num / 10;
	}
	if ((r.rssi_ok = ws_vars_get(vars, WS_V_SYS_WIFI_RSSI, &now, &v))) {
		r.rssi = v.num / 10;
	}
	ws_app_unlock();
	if (ws_sensors_json(&r, payload, sizeof(payload)) > 0) {
		publish_sub("sensors", payload, false);
	}
}

static void subscribe_all(void)
{
	static struct mqtt_topic list[ARRAY_SIZE(subs) + WS_MAX_EXT];
	static char names[ARRAY_SIZE(subs) + WS_MAX_EXT][WS_TOPIC_LEN + 24];
	struct mqtt_subscription_list sl = {0};
	int n = 0;

	for (size_t i = 0; i < ARRAY_SIZE(subs); i++, n++) {
		ws_topic(names[n], sizeof(names[n]), ws_app_device_id(), subs[i]);
	}
	ws_app_lock();
	struct ws_config *c = ws_app_cfg();

	for (int i = 0; i < c->n_ext; i++, n++) {
		snprintf(names[n], sizeof(names[n]), "%s", c->ext[i].topic);
	}
	ws_app_unlock();
	for (int i = 0; i < n; i++) {
		list[i].topic.utf8 = (const uint8_t *)names[i];
		list[i].topic.size = strlen(names[i]);
		list[i].qos = MQTT_QOS_0_AT_MOST_ONCE;
	}
	sl.list = list;
	sl.list_count = n;
	sl.message_id = msg_id++;
	int r = mqtt_subscribe(&client, &sl);

	if (r) {
		LOG_ERR("subscribe: %d", r);
	}
}

static bool topic_is(const struct mqtt_topic *t, const char *sub)
{
	char full[96];
	int n = ws_topic(full, sizeof(full), ws_app_device_id(), sub);

	return t->topic.size == (uint32_t)n && memcmp(t->topic.utf8, full, n) == 0;
}

static void reply_cfg_result(int r, const struct ws_cfg_errors *errs)
{
	struct ws_jw w;

	ws_jw_init(&w, payload, sizeof(payload));
	ws_jw_obj(&w);
	ws_jw_kbool(&w, "ok", r == 0);
	ws_jw_key(&w, "errors");
	ws_cfg_errors_json(errs, &w);
	ws_jw_obj_end(&w);
	publish_sub("display/cfg/result", payload, false);
}

static void handle(const struct mqtt_publish_param *p)
{
	const struct mqtt_topic *t = &p->message.topic;
	size_t len = p->message.payload.len;
	static struct ws_cfg_errors errs;

	if (topic_is(t, "display/cfg/set")) {
		/* large: the shared 32 KB buffer */
		char *buf = ws_app_json_buf_take(K_SECONDS(5));

		if (!buf || len > WS_MAX_FILE) {
			if (buf) {
				mqtt_readall_publish_payload(&client, (uint8_t *)buf,
							     MIN(len, WS_MAX_FILE));
				ws_app_json_buf_give();
			}
			LOG_WRN("cfg/set dropped (%u bytes)", (unsigned int)len);
			return;
		}
		int r = mqtt_readall_publish_payload(&client, (uint8_t *)buf, len);

		buf[len] = '\0';
		/* ws_app_cfg_apply does not touch the shared buffer */
		if (r == 0) {
			r = ws_app_cfg_apply(buf, len, true, &errs);
		}
		ws_app_json_buf_give();
		reply_cfg_result(r, &errs);
		return;
	}
	if (len >= sizeof(payload)) {
		/* drain and ignore */
		while (len) {
			size_t k = MIN(len, sizeof(payload));

			mqtt_readall_publish_payload(&client, (uint8_t *)payload, k);
			len -= k;
		}
		return;
	}
	if (mqtt_readall_publish_payload(&client, (uint8_t *)payload, len)) {
		return;
	}
	payload[len] = '\0';

	if (topic_is(t, "lamp/set")) {
		int on = ws_parse_onoff(payload, len);

		if (on >= 0) {
			struct ws_msg_lamp m = {on};

			zbus_chan_pub(&ws_chan_lamp_cmd, &m, K_MSEC(100));
		}
	} else if (topic_is(t, "forecast")) {
		struct ws_now now = ws_app_now();

		ws_app_lock();
		int r = ws_forecast_apply(ws_app_vars(), payload, len, now.mono);

		ws_app_unlock();
		ws_app_vars_touched();
		LOG_INF("forecast %s", r ? "rejected" : "applied");
	} else if (topic_is(t, "display/pin")) {
		int screen;
		uint32_t minutes;

		ws_app_lock();
		int r = ws_parse_pin(ws_app_cfg(), payload, len, &screen, &minutes);

		ws_app_unlock();
		if (r == 0) {
			ws_display_pin(screen, minutes);
		}
	} else if (topic_is(t, "ota")) {
		ws_ota_request_json(payload, len);
	} else {
		/* external variables */
		ws_app_lock();
		struct ws_config *c = ws_app_cfg();
		struct ws_now now = ws_app_now();

		for (int i = 0; i < c->n_ext; i++) {
			if (t->topic.size == strlen(c->ext[i].topic) &&
			    memcmp(t->topic.utf8, c->ext[i].topic, t->topic.size) == 0) {
				ws_ext_apply(ws_app_vars(), c->ext[i].idx, c->ext[i].field, payload,
					     len, now.mono);
			}
		}
		ws_app_unlock();
		ws_app_vars_touched();
	}
}

static void set_connected(bool on)
{
	struct ws_msg_mqtt m = {on};

	connected = on;
	ws_app_set_num(WS_V_SYS_MQTT, on);
	zbus_chan_pub(&ws_chan_mqtt, &m, K_MSEC(100));
}

static void evt_handler(struct mqtt_client *c, const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result == 0) {
			set_connected(true);
			LOG_INF("MQTT connected");
		} else {
			LOG_ERR("CONNACK %d", evt->result);
		}
		break;
	case MQTT_EVT_DISCONNECT:
		set_connected(false);
		break;
	case MQTT_EVT_PUBLISH:
		handle(&evt->param.publish);
		break;
	default:
		break;
	}
}

static int resolve(void)
{
	struct zsock_addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
	struct zsock_addrinfo *res;
	char port[8];

	snprintf(port, sizeof(port), "%d", set.mqtt_port);
	int r = zsock_getaddrinfo(set.mqtt_host, port, &hints, &res);

	if (r) {
		LOG_WRN("resolve %s: %d", set.mqtt_host, r);
		return -EHOSTUNREACH;
	}
	memcpy(&broker, res->ai_addr, MIN(res->ai_addrlen, sizeof(broker)));
	zsock_freeaddrinfo(res);
	return 0;
}

static int connect_broker(void)
{
	int r;

	ws_app_settings_get(&set);
	if (!set.mqtt_host[0]) {
		return -EINVAL;
	}
	r = resolve();
	if (r) {
		return r;
	}
	mqtt_client_init(&client);
	client.broker = &broker;
	client.evt_cb = evt_handler;
	client.client_id.utf8 = (const uint8_t *)ws_app_device_id();
	client.client_id.size = strlen(ws_app_device_id());
	if (set.mqtt_user[0]) {
		user.utf8 = (const uint8_t *)set.mqtt_user;
		user.size = strlen(set.mqtt_user);
		pass.utf8 = (const uint8_t *)set.mqtt_pass;
		pass.size = strlen(set.mqtt_pass);
		client.user_name = &user;
		client.password = &pass;
	}
	ws_topic(lwt_topic, sizeof(lwt_topic), ws_app_device_id(), "status");
	will_topic.topic.utf8 = (const uint8_t *)lwt_topic;
	will_topic.topic.size = strlen(lwt_topic);
	will_topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	client.will_topic = &will_topic;
	client.will_message = &will_msg;
	client.will_retain = 1;
	client.clean_session = 1;
	client.keepalive = KEEPALIVE_S;
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.rx_buf = rx_buf;
	client.rx_buf_size = sizeof(rx_buf);
	client.tx_buf = tx_buf;
	client.tx_buf_size = sizeof(tx_buf);
	client.transport.type = MQTT_TRANSPORT_NON_SECURE;

	r = mqtt_connect(&client);
	if (r) {
		LOG_WRN("connect %s:%d: %d", set.mqtt_host, set.mqtt_port, r);
		return r;
	}
	/* wait for CONNACK */
	struct zsock_pollfd fd = {.fd = client.transport.tcp.sock, .events = ZSOCK_POLLIN};

	if (zsock_poll(&fd, 1, 5000) <= 0 || mqtt_input(&client) || !connected) {
		mqtt_abort(&client);
		return -ECONNREFUSED;
	}
	return 0;
}

static void session(void)
{
	int64_t next_sensors = k_uptime_get() + 2000;

	publish_sub("status", "online", true);
	subscribe_all();
	publish_discovery();
	publish_lamp();
	publish_display();
	publish_cfg_backup();
	atomic_clear(&flags);

	while (connected) {
		struct zsock_pollfd fd = {.fd = client.transport.tcp.sock, .events = ZSOCK_POLLIN};
		int left = mqtt_keepalive_time_left(&client);
		int timeout = MIN(left, 500);

		if (zsock_poll(&fd, 1, MAX(timeout, 0)) > 0) {
			if (fd.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) {
				break;
			}
			if (mqtt_input(&client)) {
				break;
			}
		}
		int lr = mqtt_live(&client);

		if (lr && lr != -EAGAIN) {
			break;
		}
		atomic_val_t f = atomic_clear(&flags);

		if (f & F_RECONNECT) {
			mqtt_disconnect(&client, NULL);
			break;
		}
		if (f & F_LAMP) {
			publish_lamp();
		}
		if (f & F_DISPLAY) {
			publish_display();
		}
		if (f & F_CFG) {
			/* new screens: new select options, a new backup, new ext topics */
			publish_discovery();
			publish_cfg_backup();
			subscribe_all();
		}
		if ((f & F_SENSORS) || k_uptime_get() >= next_sensors) {
			publish_sensors();
			next_sensors = k_uptime_get() + SENSORS_PERIOD_MS;
		}
	}
	if (connected) {
		mqtt_abort(&client);
	}
	set_connected(false);
}

static void mqtt_thread(void *a, void *b, void *c)
{
	uint32_t backoff = 1;

	for (;;) {
		ws_net_wait_online(K_FOREVER);
		if (connect_broker() == 0) {
			backoff = 1;
			session();
			LOG_WRN("MQTT disconnected");
		} else {
			backoff = MIN(backoff * 2, BACKOFF_MAX_S);
		}
		/* a settings change wakes us up early */
		k_sem_take(&wake, K_SECONDS(backoff));
	}
}

K_THREAD_STACK_DEFINE(mqtt_stack, 6144);
static struct k_thread mqtt_tid;

int ws_mqtt_start(void)
{
	k_thread_create(&mqtt_tid, mqtt_stack, K_THREAD_STACK_SIZEOF(mqtt_stack), mqtt_thread, NULL,
			NULL, NULL, 8, 0, K_NO_WAIT);
	k_thread_name_set(&mqtt_tid, "ws_mqtt");
	return 0;
}

static int cmd_mqtt(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "connected: %s", connected ? "yes" : "no");
	shell_print(sh, "broker: %s:%d", set.mqtt_host, set.mqtt_port);
	shell_print(sh, "id: %s", ws_app_device_id());
	if (argc > 1 && !strcmp(argv[1], "publish")) {
		raise(F_SENSORS | F_DISPLAY | F_LAMP);
	}
	return 0;
}

SHELL_SUBCMD_ADD((ws), mqtt, NULL, "MQTT state; 'ws mqtt publish' sends everything now", cmd_mqtt,
		 1, 1);
