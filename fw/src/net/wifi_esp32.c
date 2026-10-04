/*
 * Wi-Fi backend for the ESP32-S3 (wifi_mgmt). The station and the access
 * point interfaces are separate (CONFIG_WIFI_USAGE_MODE_STA_AP). Scanning
 * runs before the AP starts and the results are cached: with an AP up the
 * driver may refuse to scan (open question in the plan, bench check H13).
 */
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include "wifi.h"

LOG_MODULE_REGISTER(ws_wifi, LOG_LEVEL_INF);

#define AP_ADDR "192.168.4.1"
#define AP_MASK "255.255.255.0"
#define AP_POOL "192.168.4.10"

#define WIFI_EVENTS                                                                                \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |                        \
	 NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE | NET_EVENT_WIFI_AP_STA_CONNECTED)

static ws_wifi_cb cb;
static struct net_mgmt_event_callback wifi_cb, ip_cb;
static struct net_if *sta, *ap;
static struct ws_wifi_net scan[WS_WIFI_SCAN_MAX];
static int scan_n;
static K_SEM_DEFINE(scan_sem, 0, 1);
static char ssid_buf[33], psk_buf[65];

static void on_wifi(struct net_mgmt_event_callback *c, uint64_t ev, struct net_if *iface)
{
	switch (ev) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status *st = c->info;

		if (st && st->status) {
			LOG_WRN("connect failed: %d", st->status);
			cb(WS_WIFI_EV_DISCONNECTED);
		}
		/* success is reported when DHCP gives an address */
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		cb(WS_WIFI_EV_DISCONNECTED);
		break;
	case NET_EVENT_WIFI_SCAN_RESULT: {
		const struct wifi_scan_result *r = c->info;

		if (r && scan_n < WS_WIFI_SCAN_MAX && r->ssid_length) {
			struct ws_wifi_net *n = &scan[scan_n++];

			memcpy(n->ssid, r->ssid, MIN(r->ssid_length, sizeof(n->ssid) - 1));
			n->ssid[MIN(r->ssid_length, sizeof(n->ssid) - 1)] = '\0';
			n->rssi = r->rssi;
			n->open = r->security == WIFI_SECURITY_TYPE_NONE;
		}
		break;
	}
	case NET_EVENT_WIFI_SCAN_DONE:
		k_sem_give(&scan_sem);
		cb(WS_WIFI_EV_SCAN_DONE);
		break;
	case NET_EVENT_WIFI_AP_STA_CONNECTED:
		cb(WS_WIFI_EV_AP_CLIENT);
		break;
	default:
		break;
	}
}

static void on_ip(struct net_mgmt_event_callback *c, uint64_t ev, struct net_if *iface)
{
	if (ev == NET_EVENT_IPV4_ADDR_ADD && iface == sta) {
		cb(WS_WIFI_EV_CONNECTED);
	}
}

int ws_wifi_init(ws_wifi_cb callback)
{
	cb = callback;
	sta = net_if_get_wifi_sta();
	ap = net_if_get_wifi_sap();
	if (!sta) {
		LOG_ERR("no Wi-Fi station interface");
		return -ENODEV;
	}
	net_mgmt_init_event_callback(&wifi_cb, on_wifi, WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ip_cb, on_ip, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ip_cb);
	return 0;
}

int ws_wifi_connect(const char *ssid, const char *psk)
{
	struct wifi_connect_req_params p = {0};

	strncpy(ssid_buf, ssid, sizeof(ssid_buf) - 1);
	strncpy(psk_buf, psk, sizeof(psk_buf) - 1);
	p.ssid = (const uint8_t *)ssid_buf;
	p.ssid_length = strlen(ssid_buf);
	p.psk = (const uint8_t *)psk_buf;
	p.psk_length = strlen(psk_buf);
	p.security = p.psk_length ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
	p.channel = WIFI_CHANNEL_ANY;
	p.band = WIFI_FREQ_BAND_2_4_GHZ;
	p.mfp = WIFI_MFP_OPTIONAL;
	LOG_INF("connecting to '%s'", ssid_buf);
	int r = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta, &p, sizeof(p));

	if (r) {
		LOG_ERR("connect request: %d", r);
	}
	return r;
}

int ws_wifi_disconnect(void)
{
	return net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta, NULL, 0);
}

int ws_wifi_scan(void)
{
	scan_n = 0;
	k_sem_reset(&scan_sem);
	int r = net_mgmt(NET_REQUEST_WIFI_SCAN, sta, NULL, 0);

	if (r) {
		LOG_WRN("scan request: %d", r);
		return r;
	}
	return k_sem_take(&scan_sem, K_SECONDS(10));
}

int ws_wifi_scan_results(struct ws_wifi_net *out, int max)
{
	int n = MIN(max, scan_n);

	memcpy(out, scan, n * sizeof(scan[0]));
	return n;
}

int ws_wifi_ap_start(const char *ssid)
{
	struct wifi_connect_req_params p = {0};
	struct in_addr addr, mask, pool;
	struct net_if *iface = ap ? ap : sta;

	net_addr_pton(AF_INET, AP_ADDR, &addr);
	net_addr_pton(AF_INET, AP_MASK, &mask);
	net_addr_pton(AF_INET, AP_POOL, &pool);
	net_if_ipv4_set_gw(iface, &addr);
	net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
	net_if_ipv4_set_netmask_by_addr(iface, &addr, &mask);

	strncpy(ssid_buf, ssid, sizeof(ssid_buf) - 1);
	p.ssid = (const uint8_t *)ssid_buf;
	p.ssid_length = strlen(ssid_buf);
	p.security = WIFI_SECURITY_TYPE_NONE;
	p.channel = WIFI_CHANNEL_ANY;
	p.band = WIFI_FREQ_BAND_2_4_GHZ;
	int r = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &p, sizeof(p));

	if (r) {
		LOG_ERR("AP enable: %d", r);
		return r;
	}
	r = net_dhcpv4_server_start(iface, &pool);
	LOG_INF("access point '%s' at " AP_ADDR " (dhcp %d)", ssid_buf, r);
	return 0;
}

int ws_wifi_ap_stop(void)
{
	struct net_if *iface = ap ? ap : sta;

	net_dhcpv4_server_stop(iface);
	return net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);
}

int ws_wifi_rssi(void)
{
	struct wifi_iface_status st = {0};

	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, sta, &st, sizeof(st))) {
		return 0;
	}
	return st.rssi;
}

void ws_wifi_ip(char *buf, int len)
{
	struct in_addr *a = net_if_ipv4_get_global_addr(sta, NET_ADDR_PREFERRED);

	buf[0] = '\0';
	if (a) {
		net_addr_ntop(AF_INET, a, buf, len);
	}
}
