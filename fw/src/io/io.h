/* Lamp key, service button and status LED (F5). */
#ifndef WS_IO_H_
#define WS_IO_H_

#include <stdbool.h>

#include <ws/ui.h>

int ws_io_start(void);
/* Button state at power-up (read before the services start). */
bool ws_io_button_held_at_boot(void);
void ws_io_lamp(bool on);
bool ws_io_lamp_state(void);
/* LED pattern; WS_LED_COUNT returns control to the network state. */
void ws_io_led_override(enum ws_led_pattern p);
void ws_io_led_set_auto(enum ws_led_pattern p);

#endif
