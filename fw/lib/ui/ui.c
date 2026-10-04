/* Button and LED state machines, see ws/ui.h. */
#include <string.h>

#include <ws/ui.h>

void ws_button_init(struct ws_button *b, bool held_at_boot, int64_t now_ms)
{
	memset(b, 0, sizeof(*b));
	b->pressed = held_at_boot;
	b->boot_window = held_at_boot;
	b->down_ms = now_ms;
}

enum ws_btn_event ws_button_edge(struct ws_button *b, bool pressed, int64_t now_ms)
{
	if (pressed == b->pressed) {
		return WS_BTN_NONE;
	}
	b->pressed = pressed;
	if (pressed) {
		b->down_ms = now_ms;
		b->long_sent = false;
		return WS_BTN_NONE;
	}
	/* release */
	if (b->boot_window) {
		/* the press that started at power-up is consumed by the boot event */
		b->boot_window = false;
		return WS_BTN_NONE;
	}
	int64_t held = now_ms - b->down_ms;

	if (b->long_sent || held < WS_BTN_GLITCH_MS) {
		return WS_BTN_NONE;
	}
	return held < WS_BTN_LONG_MS ? WS_BTN_SHORT : WS_BTN_NONE;
}

enum ws_btn_event ws_button_poll(struct ws_button *b, int64_t now_ms)
{
	if (b->boot_window) {
		/* held at power-up: report once, as soon as we look */
		if (!b->long_sent) {
			b->long_sent = true;
			return WS_BTN_BOOT_HELD;
		}
		return WS_BTN_NONE;
	}
	if (b->pressed && !b->long_sent && now_ms - b->down_ms >= WS_BTN_LONG_MS) {
		b->long_sent = true;
		return WS_BTN_LONG;
	}
	return WS_BTN_NONE;
}

static const char *const led_names[WS_LED_COUNT] = {
	"off", "on", "heartbeat", "slow", "ap", "fast", "ota",
};

const char *ws_led_name(enum ws_led_pattern p)
{
	return (unsigned int)p < WS_LED_COUNT ? led_names[p] : "";
}

int ws_led_parse(const char *s)
{
	for (int i = 0; i < WS_LED_COUNT; i++) {
		if (strcmp(s, led_names[i]) == 0) {
			return i;
		}
	}
	return -1;
}

bool ws_led_level(enum ws_led_pattern p, int64_t now_ms)
{
	int64_t t;

	switch (p) {
	case WS_LED_ON:
		return true;
	case WS_LED_HEARTBEAT:
		return now_ms % 2000 < 60;
	case WS_LED_SLOW:
		return now_ms % 1000 < 500;
	case WS_LED_AP:
		t = now_ms % 1500;
		return t < 100 || (t >= 250 && t < 350);
	case WS_LED_FAST:
		return now_ms % 200 < 100;
	case WS_LED_OTA:
		return now_ms % 500 < 250;
	default:
		return false;
	}
}
