/*
 * Task watchdog for the service threads (plan F10). Each thread that loops
 * registers a channel and feeds it every iteration; a thread that stops
 * feeding reboots the board with its name in the log. During the OTA
 * self-test such a reboot leaves the image unconfirmed and MCUboot reverts.
 */
#ifndef WS_WATCHDOG_H_
#define WS_WATCHDOG_H_

#include <stdint.h>

#ifdef CONFIG_WS_WATCHDOG
/* Returns a channel id (>= 0) or a negative errno. */
int ws_wdt_add(const char *name, uint32_t timeout_ms);
void ws_wdt_feed(int ch);
#else
static inline int ws_wdt_add(const char *name, uint32_t timeout_ms)
{
	return -1;
}
static inline void ws_wdt_feed(int ch)
{
}
#endif

#endif
