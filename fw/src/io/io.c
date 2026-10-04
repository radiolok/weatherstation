/*
 * Lamp key (GPIO10), service button (GPIO11 through gpio-keys) and status LED
 * (GPIO12). Button and LED logic is in fw/lib/ui.
 */
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include "app.h"
#include "io.h"

LOG_MODULE_REGISTER(ws_io, LOG_LEVEL_INF);

static const struct gpio_dt_spec lamp = GPIO_DT_SPEC_GET(DT_ALIAS(ws_lamp), gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(ws_led), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(ws_button), gpios);

#define BUTTON_KEYS DT_PARENT(DT_ALIAS(ws_button))
#define BUTTON_CODE DT_PROP(DT_ALIAS(ws_button), zephyr_code)

static struct ws_button btn;
static bool boot_held;
static bool lamp_on;
static enum ws_led_pattern led_auto = WS_LED_SLOW;
static enum ws_led_pattern led_forced = WS_LED_COUNT;
static struct k_work lamp_store_work;

/* ---- lamp ---- */

static void lamp_store(struct k_work *w)
{
	struct ws_settings s;

	ws_app_settings_get(&s);
	if (s.lamp_restore == WS_LAMP_RESTORE_LAST) {
		ws_app_settings_set_int("lamp.last", lamp_on);
	}
}

void ws_io_lamp(bool on)
{
	struct ws_msg_lamp m = {on};

	gpio_pin_set_dt(&lamp, on);
	lamp_on = on;
	ws_app_set_num(WS_V_SYS_LAMP, on);
	zbus_chan_pub(&ws_chan_lamp_state, &m, K_MSEC(50));
	k_work_submit(&lamp_store_work);
	LOG_INF("lamp %s", on ? "on" : "off");
}

bool ws_io_lamp_state(void)
{
	return lamp_on;
}

static void on_lamp_cmd(const struct zbus_channel *chan)
{
	const struct ws_msg_lamp *m = zbus_chan_const_msg(chan);

	ws_io_lamp(m->on);
}

ZBUS_LISTENER_DEFINE(ws_io_lamp_lis, on_lamp_cmd);
ZBUS_CHAN_ADD_OBS(ws_chan_lamp_cmd, ws_io_lamp_lis, 3);

/* ---- LED ---- */

static void led_tick(struct k_timer *t)
{
	enum ws_led_pattern p = led_forced != WS_LED_COUNT ? led_forced : led_auto;

	gpio_pin_set_dt(&led, ws_led_level(p, k_uptime_get()));
}

K_TIMER_DEFINE(led_timer, led_tick, NULL);

void ws_io_led_override(enum ws_led_pattern p)
{
	led_forced = p;
}

void ws_io_led_set_auto(enum ws_led_pattern p)
{
	led_auto = p;
}

/* ---- button ---- */

static void publish_button(enum ws_btn_event ev)
{
	struct ws_msg_button m;

	switch (ev) {
	case WS_BTN_SHORT:
		m.event = WS_BTNMSG_SHORT;
		break;
	case WS_BTN_LONG:
		m.event = WS_BTNMSG_LONG;
		break;
	case WS_BTN_BOOT_HELD:
		m.event = WS_BTNMSG_BOOT;
		break;
	default:
		return;
	}
	LOG_INF("button %s", ev == WS_BTN_SHORT ? "short" : ev == WS_BTN_LONG ? "long" : "boot");
	zbus_chan_pub(&ws_chan_button, &m, K_MSEC(50));
}

static void on_input(struct input_event *evt, void *user_data)
{
	if (evt->type != INPUT_EV_KEY || evt->code != BUTTON_CODE) {
		return;
	}
	publish_button(ws_button_edge(&btn, evt->value != 0, k_uptime_get()));
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(BUTTON_KEYS), on_input, NULL);

static void button_poll(struct k_timer *t)
{
	publish_button(ws_button_poll(&btn, k_uptime_get()));
}

K_TIMER_DEFINE(button_timer, button_poll, NULL);

bool ws_io_button_held_at_boot(void)
{
	return boot_held;
}

int ws_io_start(void)
{
	struct ws_settings s;

	/* the key is off until we decide otherwise (bench check H9) */
	if (!gpio_is_ready_dt(&lamp) || gpio_pin_configure_dt(&lamp, GPIO_OUTPUT_INACTIVE)) {
		LOG_ERR("lamp GPIO not ready");
	}
	if (!gpio_is_ready_dt(&led) || gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE)) {
		LOG_ERR("LED GPIO not ready");
	}
	k_work_init(&lamp_store_work, lamp_store);
	/* gpio-keys already configured the pin as input */
	boot_held = gpio_is_ready_dt(&button) && gpio_pin_get_dt(&button) > 0;
	ws_button_init(&btn, boot_held, k_uptime_get());

	ws_app_settings_get(&s);
	bool on = s.lamp_restore == WS_LAMP_RESTORE_ON ||
		  (s.lamp_restore == WS_LAMP_RESTORE_LAST && s.lamp_last);

	ws_io_lamp(on);
	k_timer_start(&led_timer, K_MSEC(50), K_MSEC(50));
	k_timer_start(&button_timer, K_MSEC(100), K_MSEC(100));
	LOG_INF("io ready, button at boot: %s", boot_held ? "held" : "released");
	return 0;
}

/* ---- shell ---- */

static int cmd_lamp(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		struct ws_msg_lamp m = {strcmp(argv[1], "on") == 0};

		zbus_chan_pub(&ws_chan_lamp_cmd, &m, K_MSEC(100));
	}
	shell_print(sh, "lamp: %s", ws_io_lamp_state() ? "on" : "off");
	return 0;
}

static int cmd_led(const struct shell *sh, size_t argc, char **argv)
{
	if (strcmp(argv[1], "auto") == 0) {
		ws_io_led_override(WS_LED_COUNT);
		return 0;
	}
	int p = ws_led_parse(argv[1]);

	if (p < 0) {
		shell_error(sh, "patterns: off on heartbeat slow ap fast ota auto");
		return -EINVAL;
	}
	ws_io_led_override(p);
	return 0;
}

SHELL_SUBCMD_ADD((ws), lamp, NULL, "Lamp: lamp [on|off]", cmd_lamp, 1, 1);
SHELL_SUBCMD_ADD((ws), led, NULL, "LED pattern: led <pattern|auto>", cmd_led, 2, 0);
