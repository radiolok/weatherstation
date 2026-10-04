/* MQTT service (F7). */
#ifndef WS_MQTT_SVC_H_
#define WS_MQTT_SVC_H_

#include <stdbool.h>

#ifdef CONFIG_WS_MQTT
bool ws_mqtt_connected(void);
void ws_mqtt_request_sensors(void);
#else
static inline bool ws_mqtt_connected(void)
{
	return false;
}
static inline void ws_mqtt_request_sensors(void)
{
}
#endif

#endif
