/* STA <-> AP state machine, see ws/netfsm.h. */
#include <string.h>

#include <ws/netfsm.h>

static enum ws_led_pattern led_of(enum ws_net_state st)
{
	switch (st) {
	case WS_NET_ONLINE:
		return WS_LED_HEARTBEAT;
	case WS_NET_AP:
		return WS_LED_AP;
	case WS_NET_CONNECTING:
		return WS_LED_SLOW;
	default:
		return WS_LED_OFF;
	}
}

static struct ws_net_out out(const struct ws_netfsm *f, uint32_t act)
{
	return (struct ws_net_out){act, led_of(f->st)};
}

const char *ws_net_state_name(enum ws_net_state st)
{
	switch (st) {
	case WS_NET_START:
		return "start";
	case WS_NET_CONNECTING:
		return "connecting";
	case WS_NET_ONLINE:
		return "online";
	case WS_NET_AP:
		return "ap";
	default:
		return "";
	}
}

void ws_netfsm_init(struct ws_netfsm *f)
{
	memset(f, 0, sizeof(*f));
	f->st = WS_NET_START;
	f->backoff_s = WS_NET_BACKOFF_MIN_S;
	f->next_connect = -1;
}

static uint32_t to_ap(struct ws_netfsm *f, uint32_t extra, int64_t now)
{
	f->st = WS_NET_AP;
	f->ap_activity = now;
	f->next_connect = -1;
	/* the scan runs before the AP starts: with an AP up, the ESP32 may not
	 * scan reliably (fw/docs/implementation-plan.md, open questions) */
	return extra | WS_NETACT_SCAN | WS_NETACT_AP_START;
}

static uint32_t to_connecting(struct ws_netfsm *f, int64_t now)
{
	f->st = WS_NET_CONNECTING;
	f->backoff_s = WS_NET_BACKOFF_MIN_S;
	f->next_connect = -1;
	return WS_NETACT_CONNECT;
}

struct ws_net_out ws_netfsm_boot(struct ws_netfsm *f, bool has_config, bool button_held,
				 int64_t now)
{
	ws_netfsm_init(f);
	f->has_config = has_config;
	if (!has_config || button_held) {
		return out(f, to_ap(f, 0, now));
	}
	return out(f, to_connecting(f, now));
}

struct ws_net_out ws_netfsm_event(struct ws_netfsm *f, enum ws_net_event ev, bool has_config,
				  int64_t now)
{
	uint32_t act = 0;

	f->has_config = has_config;
	switch (ev) {
	case WS_NETEV_CONNECTED:
		if (f->st == WS_NET_CONNECTING) {
			f->st = WS_NET_ONLINE;
			f->backoff_s = WS_NET_BACKOFF_MIN_S;
			f->failures = 0;
			f->next_connect = -1;
			act = WS_NETACT_ONLINE;
		}
		break;
	case WS_NETEV_DISCONNECTED:
		if (f->st == WS_NET_ONLINE || f->st == WS_NET_CONNECTING) {
			if (f->st == WS_NET_ONLINE) {
				act |= WS_NETACT_OFFLINE;
				f->backoff_s = WS_NET_BACKOFF_MIN_S;
			}
			f->st = WS_NET_CONNECTING;
			f->failures++;
			f->next_connect = now + f->backoff_s;
			f->backoff_s = f->backoff_s * 2 > WS_NET_BACKOFF_MAX_S
					       ? WS_NET_BACKOFF_MAX_S
					       : f->backoff_s * 2;
		}
		break;
	case WS_NETEV_BUTTON_LONG:
		if (f->st != WS_NET_AP) {
			uint32_t extra = f->st == WS_NET_ONLINE ? WS_NETACT_OFFLINE : 0;

			act = to_ap(f, extra | WS_NETACT_DISCONNECT, now);
		}
		break;
	case WS_NETEV_SAVED:
		if (f->st == WS_NET_AP && has_config) {
			act = WS_NETACT_AP_STOP | to_connecting(f, now);
		} else if (f->st != WS_NET_AP && has_config) {
			/* settings changed while connected: reconnect */
			act = (f->st == WS_NET_ONLINE ? WS_NETACT_OFFLINE : 0) |
			      WS_NETACT_DISCONNECT | to_connecting(f, now);
		}
		break;
	case WS_NETEV_ACTIVITY:
		if (f->st == WS_NET_AP) {
			f->ap_activity = now;
		}
		break;
	case WS_NETEV_TICK:
		if (f->st == WS_NET_CONNECTING && f->next_connect >= 0 && now >= f->next_connect) {
			f->next_connect = -1;
			act = WS_NETACT_CONNECT;
		} else if (f->st == WS_NET_AP && now - f->ap_activity >= WS_NET_AP_IDLE_S) {
			if (has_config) {
				act = WS_NETACT_AP_STOP | to_connecting(f, now);
			} else {
				f->ap_activity = now; /* nothing to return to */
			}
		}
		break;
	default:
		break;
	}
	return out(f, act);
}
