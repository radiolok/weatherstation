/*
 * Service button and status LED as plain state machines (fw/README.md,
 * "Wi-Fi и первичная настройка"). The io service feeds raw press/release
 * edges from gpio-keys and polls the LED pattern with the uptime.
 */
#ifndef WS_UI_H_
#define WS_UI_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS_BTN_LONG_MS   5000 /* long press: access point */
#define WS_BTN_GLITCH_MS 40   /* shorter pulses are ignored */

enum ws_btn_event {
	WS_BTN_NONE,
	WS_BTN_SHORT,     /* released before 5 s: next screen (debug) */
	WS_BTN_LONG,      /* held 5 s: access point; reported once, while held */
	WS_BTN_BOOT_HELD, /* held at power-up: access point right away */
};

struct ws_button {
	bool pressed;
	bool long_sent;
	bool boot_window;
	int64_t down_ms;
};

/* `held_at_boot`: level of the button when the firmware starts. */
void ws_button_init(struct ws_button *b, bool held_at_boot, int64_t now_ms);
/* Edge from the input subsystem. */
enum ws_btn_event ws_button_edge(struct ws_button *b, bool pressed, int64_t now_ms);
/* Periodic poll (every 100 ms is plenty) for the long press. */
enum ws_btn_event ws_button_poll(struct ws_button *b, int64_t now_ms);

enum ws_led_pattern {
	WS_LED_OFF,
	WS_LED_ON,
	WS_LED_HEARTBEAT, /* connected: short blink every 2 s */
	WS_LED_SLOW,      /* connecting: 0.5 s on / 0.5 s off */
	WS_LED_AP,        /* access point: two short blinks every 1.5 s */
	WS_LED_FAST,      /* error: 100 ms on / 100 ms off */
	WS_LED_OTA,       /* update in progress: 250 / 250 */
	WS_LED_COUNT
};

bool ws_led_level(enum ws_led_pattern p, int64_t now_ms);
const char *ws_led_name(enum ws_led_pattern p);
int ws_led_parse(const char *s);

#ifdef __cplusplus
}
#endif

#endif
