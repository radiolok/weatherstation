/* Wi-Fi backend for native_sim: host sockets (NSOS) are always "connected". */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "wifi.h"

LOG_MODULE_REGISTER(ws_wifi_none, LOG_LEVEL_INF);

static ws_wifi_cb cb;
static struct k_work_delayable connected_work;

static void connected(struct k_work *w)
{
	if (cb) {
		cb(WS_WIFI_EV_CONNECTED);
	}
}

int ws_wifi_init(ws_wifi_cb callback)
{
	cb = callback;
	k_work_init_delayable(&connected_work, connected);
	return 0;
}

int ws_wifi_connect(const char *ssid, const char *psk)
{
	LOG_INF("native_sim: host network, connect to '%s' is simulated", ssid);
	k_work_reschedule(&connected_work, K_MSEC(100));
	return 0;
}

int ws_wifi_disconnect(void)
{
	k_work_cancel_delayable(&connected_work);
	return 0;
}

int ws_wifi_scan(void)
{
	return 0;
}

/* A fixed list so that the setup page has something to show */
int ws_wifi_scan_results(struct ws_wifi_net *out, int max)
{
	static const struct ws_wifi_net list[] = {
		{"home-iot", -48, false},
		{"neighbour", -81, false},
		{"cafe-free", -77, true},
	};
	int n = MIN(max, (int)ARRAY_SIZE(list));

	memcpy(out, list, n * sizeof(list[0]));
	return n;
}

int ws_wifi_ap_start(const char *ssid)
{
	LOG_INF("native_sim: access point '%s' is simulated", ssid);
	return 0;
}

int ws_wifi_ap_stop(void)
{
	return 0;
}

int ws_wifi_rssi(void)
{
	return 0;
}

void ws_wifi_ip(char *buf, int len)
{
	strncpy(buf, "127.0.0.1", len - 1);
	buf[len - 1] = '\0';
}
