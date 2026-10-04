/*
 * Network manager: feeds Wi-Fi, button and settings events into the STA/AP
 * state machine (fw/lib/net) and performs its actions.
 */
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <ws/netfsm.h>

#include "app.h"
#include "io/io.h"
#include "net_mgr.h"

LOG_MODULE_REGISTER(ws_net, LOG_LEVEL_INF);

K_MSGQ_DEFINE(net_q, sizeof(uint8_t), 16, 1);
static K_EVENT_DEFINE(online_ev);
static struct ws_netfsm fsm;
static bool ap_at_boot_flag;

static void post(enum ws_net_event ev)
{
	uint8_t e = ev;

	k_msgq_put(&net_q, &e, K_NO_WAIT);
}

static void on_wifi(enum ws_wifi_event ev)
{
	switch (ev) {
	case WS_WIFI_EV_CONNECTED:
		post(WS_NETEV_CONNECTED);
		break;
	case WS_WIFI_EV_DISCONNECTED:
		post(WS_NETEV_DISCONNECTED);
		break;
	case WS_WIFI_EV_AP_CLIENT:
		post(WS_NETEV_ACTIVITY);
		break;
	default:
		break;
	}
}

static void on_button(const struct zbus_channel *chan)
{
	const struct ws_msg_button *m = zbus_chan_const_msg(chan);

	if (m->event == WS_BTNMSG_LONG) {
		post(WS_NETEV_BUTTON_LONG);
	}
}

ZBUS_LISTENER_DEFINE(ws_net_btn_lis, on_button);
ZBUS_CHAN_ADD_OBS(ws_chan_button, ws_net_btn_lis, 3);

static void on_settings(const struct zbus_channel *chan)
{
	const struct ws_msg_settings *m = zbus_chan_const_msg(chan);
	/* wifi.ssid and wifi.psk are the first two fields */
	if (m->changed & 0x3) {
		post(WS_NETEV_SAVED);
	}
}

ZBUS_LISTENER_DEFINE(ws_net_set_lis, on_settings);
ZBUS_CHAN_ADD_OBS(ws_chan_settings, ws_net_set_lis, 3);

void ws_net_activity(void)
{
	if (fsm.st == WS_NET_AP) {
		post(WS_NETEV_ACTIVITY);
	}
}

const char *ws_net_state_str(void)
{
	return ws_net_state_name(fsm.st);
}

bool ws_net_online(void)
{
	return fsm.st == WS_NET_ONLINE;
}

bool ws_net_wait_online(k_timeout_t timeout)
{
	return k_event_wait(&online_ev, 1, false, timeout) != 0;
}

int ws_net_scan_results(struct ws_wifi_net *out, int max)
{
	return ws_wifi_scan_results(out, max);
}

static bool has_config(void)
{
#ifdef CONFIG_WS_NET_WIFI
	struct ws_settings s;

	ws_app_settings_get(&s);
	return s.wifi_ssid[0] != '\0';
#else
	return true; /* native_sim: the host network is always there */
#endif
}

static void publish(enum ws_net_status st)
{
	struct ws_msg_net m = {.status = st};

	m.rssi = (int8_t)ws_wifi_rssi();
	if (st == WS_NETST_ONLINE) {
		ws_wifi_ip(m.ip, sizeof(m.ip));
	} else if (st == WS_NETST_AP) {
		strcpy(m.ip, "192.168.4.1");
	}
	zbus_chan_pub(&ws_chan_net, &m, K_MSEC(100));
}

static void run(struct ws_net_out o)
{
	struct ws_settings s;

	ws_io_led_set_auto(o.led);
	if (o.act & WS_NETACT_OFFLINE) {
		k_event_clear(&online_ev, 1);
		publish(WS_NETST_CONNECTING);
	}
	if (o.act & WS_NETACT_DISCONNECT) {
		ws_wifi_disconnect();
	}
	if (o.act & WS_NETACT_AP_STOP) {
		ws_wifi_ap_stop();
	}
	if (o.act & WS_NETACT_SCAN) {
		ws_wifi_scan();
	}
	if (o.act & WS_NETACT_AP_START) {
		char ssid[32];
		const char *id = ws_app_device_id();
		size_t n = strlen(id);

		snprintf(ssid, sizeof(ssid), "Weatherstation-%s", n >= 4 ? id + n - 4 : id);
		ws_wifi_ap_start(ssid);
		publish(WS_NETST_AP);
	}
	if (o.act & WS_NETACT_CONNECT) {
		ws_app_settings_get(&s);
		publish(WS_NETST_CONNECTING);
		ws_wifi_connect(s.wifi_ssid, s.wifi_psk);
	}
	if (o.act & WS_NETACT_ONLINE) {
		publish(WS_NETST_ONLINE);
		k_event_post(&online_ev, 1);
	}
}

static void net_thread(void *a, void *b, void *c)
{
	uint8_t ev;
	int64_t last_rssi = 0;

	ws_wifi_init(on_wifi);
	run(ws_netfsm_boot(&fsm, has_config(), ap_at_boot_flag, ws_app_now().mono));
	for (;;) {
		int64_t now;

		if (k_msgq_get(&net_q, &ev, K_SECONDS(1)) == 0) {
			now = ws_app_now().mono;
			run(ws_netfsm_event(&fsm, ev, has_config(), now));
		}
		now = ws_app_now().mono;
		run(ws_netfsm_event(&fsm, WS_NETEV_TICK, has_config(), now));
		if (fsm.st == WS_NET_ONLINE && now - last_rssi >= 30) {
			int rssi = ws_wifi_rssi();

			last_rssi = now;
			if (rssi) {
				ws_app_set_num(WS_V_SYS_WIFI_RSSI, rssi * 10);
			}
		}
	}
}

K_THREAD_STACK_DEFINE(net_stack, 4096);
static struct k_thread net_tid;

int ws_net_start(bool ap_at_boot)
{
	ap_at_boot_flag = ap_at_boot;
	k_thread_create(&net_tid, net_stack, K_THREAD_STACK_SIZEOF(net_stack), net_thread, NULL,
			NULL, NULL, 7, 0, K_NO_WAIT);
	k_thread_name_set(&net_tid, "ws_net");
	return ws_timesync_start();
}

static int cmd_net(const struct shell *sh, size_t argc, char **argv)
{
	struct ws_wifi_net nets[WS_WIFI_SCAN_MAX];
	int n = ws_wifi_scan_results(nets, WS_WIFI_SCAN_MAX);

	shell_print(sh, "state: %s", ws_net_state_str());
	shell_print(sh, "backoff: %u", fsm.backoff_s);
	for (int i = 0; i < n; i++) {
		shell_print(sh, "scan: %s %d%s", nets[i].ssid, nets[i].rssi,
			    nets[i].open ? " open" : "");
	}
	return 0;
}

static int cmd_ap(const struct shell *sh, size_t argc, char **argv)
{
	post(WS_NETEV_BUTTON_LONG);
	return 0;
}

SHELL_SUBCMD_ADD((ws), net, NULL, "Network state", cmd_net, 1, 0);
SHELL_SUBCMD_ADD((ws), ap, NULL, "Start the access point (like the 5 s press)", cmd_ap, 1, 0);
