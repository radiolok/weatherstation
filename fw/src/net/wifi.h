/*
 * Wi-Fi backend used by the network manager. wifi_esp32.c talks to the
 * ESP32 driver through wifi_mgmt; wifi_none.c (native_sim) has host sockets
 * (NSOS) and treats "connect" as an immediate success.
 */
#ifndef WS_WIFI_H_
#define WS_WIFI_H_

#include <stdbool.h>
#include <stdint.h>

#define WS_WIFI_SCAN_MAX 16

struct ws_wifi_net {
	char ssid[33];
	int8_t rssi;
	bool open;
};

enum ws_wifi_event {
	WS_WIFI_EV_CONNECTED, /* IP address received */
	WS_WIFI_EV_DISCONNECTED,
	WS_WIFI_EV_SCAN_DONE,
	WS_WIFI_EV_AP_CLIENT, /* a phone joined the access point */
};

typedef void (*ws_wifi_cb)(enum ws_wifi_event ev);

int ws_wifi_init(ws_wifi_cb cb);
int ws_wifi_connect(const char *ssid, const char *psk);
int ws_wifi_disconnect(void);
/* Blocking scan of up to WS_WIFI_SCAN_MAX networks into the cache. */
int ws_wifi_scan(void);
int ws_wifi_scan_results(struct ws_wifi_net *out, int max);
int ws_wifi_ap_start(const char *ssid);
int ws_wifi_ap_stop(void);
int ws_wifi_rssi(void); /* dBm, 0 if unknown */
/* IPv4 address of the station interface, "" if none */
void ws_wifi_ip(char *buf, int len);

#endif
