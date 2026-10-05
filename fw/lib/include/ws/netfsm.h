/*
 * Network state machine STA <-> AP (fw/img/fw-wifi-states.svg): events in,
 * actions out. The net service turns actions into Wi-Fi management calls.
 */
#ifndef WS_NETFSM_H_
#define WS_NETFSM_H_

#include <stdbool.h>
#include <stdint.h>

#include <ws/ui.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS_NET_BACKOFF_MIN_S 1
#define WS_NET_BACKOFF_MAX_S 60
#define WS_NET_AP_IDLE_S     600 /* back to the network after 10 min without activity */

enum ws_net_state {
	WS_NET_START,
	WS_NET_CONNECTING,
	WS_NET_ONLINE,
	WS_NET_AP,
};

enum ws_net_event {
	WS_NETEV_BOOT,         /* arg: has_config, button held at boot */
	WS_NETEV_CONNECTED,    /* IP address received */
	WS_NETEV_DISCONNECTED, /* link lost or connect failed */
	WS_NETEV_BUTTON_LONG,
	WS_NETEV_SAVED,    /* new Wi-Fi settings saved in the AP page */
	WS_NETEV_ACTIVITY, /* web request or station joined in AP mode */
	WS_NETEV_TICK,
};

/* Action bits */
#define WS_NETACT_CONNECT    0x01 /* start STA connection with the stored config */
#define WS_NETACT_DISCONNECT 0x02
#define WS_NETACT_SCAN       0x04 /* scan and cache the list before the AP starts */
#define WS_NETACT_AP_START   0x08
#define WS_NETACT_AP_STOP    0x10
#define WS_NETACT_ONLINE     0x20 /* start MQTT, SNTP, mDNS */
#define WS_NETACT_OFFLINE    0x40

struct ws_netfsm {
	enum ws_net_state st;
	bool has_config;
	uint32_t backoff_s;
	int64_t next_connect; /* -1: none pending */
	int64_t ap_activity;
	uint32_t failures;
};

struct ws_net_out {
	uint32_t act;
	enum ws_led_pattern led;
};

void ws_netfsm_init(struct ws_netfsm *f);
struct ws_net_out ws_netfsm_boot(struct ws_netfsm *f, bool has_config, bool button_held,
				 int64_t now);
struct ws_net_out ws_netfsm_event(struct ws_netfsm *f, enum ws_net_event ev, bool has_config,
				  int64_t now);
const char *ws_net_state_name(enum ws_net_state st);

#ifdef __cplusplus
}
#endif

#endif
