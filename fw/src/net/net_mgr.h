/* Network manager (F6): STA <-> AP machine from fw/lib/net. */
#ifndef WS_NET_MGR_H_
#define WS_NET_MGR_H_

#include <stdbool.h>

#include "wifi.h"

int ws_net_start(bool ap_at_boot);
/* Web page activity in AP mode keeps the access point up. */
void ws_net_activity(void);
const char *ws_net_state_str(void);
bool ws_net_online(void);
int ws_net_scan_results(struct ws_wifi_net *out, int max);
/* Blocks until online or timeout; true when online. */
bool ws_net_wait_online(k_timeout_t timeout);

int ws_timesync_start(void);

#endif
