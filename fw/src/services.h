/*
 * Start functions of the optional services. A service that is disabled in
 * Kconfig gets an empty inline stub, so main() stays the same everywhere.
 */
#ifndef WS_SERVICES_H_
#define WS_SERVICES_H_

#include <stdbool.h>

#ifdef CONFIG_WS_NET
int ws_net_start(bool ap_at_boot);
#else
static inline int ws_net_start(bool ap_at_boot)
{
	return 0;
}
#endif

#ifdef CONFIG_WS_MQTT
int ws_mqtt_start(void);
#else
static inline int ws_mqtt_start(void)
{
	return 0;
}
#endif

#ifdef CONFIG_WS_WEB
int ws_web_start(void);
#else
static inline int ws_web_start(void)
{
	return 0;
}
#endif

#ifdef CONFIG_WS_METAR
int ws_metar_start(void);
#else
static inline int ws_metar_start(void)
{
	return 0;
}
#endif

#ifdef CONFIG_WS_OTA
int ws_ota_boot_check(void);
#else
static inline int ws_ota_boot_check(void)
{
	return 0;
}
#endif

#ifdef CONFIG_WS_WATCHDOG
int ws_watchdog_start(void);
#else
static inline int ws_watchdog_start(void)
{
	return 0;
}
#endif

#endif
