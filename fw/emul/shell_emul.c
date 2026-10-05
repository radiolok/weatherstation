/* "ws emul" shell commands: drive the native_sim emulators from tests. */
#include <stdlib.h>
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "emul.h"

static const struct gpio_dt_spec lamp = GPIO_DT_SPEC_GET(DT_ALIAS(ws_lamp), gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(ws_led), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(ws_button), gpios);

/* ws emul thp <t degC> <p Pa> <rh %> | ws emul thp fail|ok */
static int cmd_thp(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 2) {
		ws_emul_bme280_fail(strcmp(argv[1], "fail") == 0);
		return 0;
	}
	if (argc != 4) {
		shell_error(sh, "thp <t> <p_pa> <rh> | thp fail|ok");
		return -EINVAL;
	}
	double t = strtod(argv[1], NULL), rh = strtod(argv[3], NULL);

	ws_emul_bme280_set((int32_t)(t * 100 + (t >= 0 ? 0.5 : -0.5)),
			   (uint32_t)strtoul(argv[2], NULL, 10), (uint32_t)(rh * 10 + 0.5));
	return 0;
}

/* ws emul co2 <ppm> | bad | ok */
static int cmd_co2(const struct shell *sh, size_t argc, char **argv)
{
	if (!strcmp(argv[1], "bad") || !strcmp(argv[1], "ok")) {
		ws_emul_mhz19b_fail(!strcmp(argv[1], "bad"));
		return 0;
	}
	ws_emul_mhz19b_set((uint16_t)strtoul(argv[1], NULL, 10));
	return 0;
}

/* ws emul button down|up|press <ms> (the button pulls the line low) */
static int cmd_button(const struct shell *sh, size_t argc, char **argv)
{
	if (!strcmp(argv[1], "down")) {
		gpio_emul_input_set_dt(&button, 0);
	} else if (!strcmp(argv[1], "up")) {
		gpio_emul_input_set_dt(&button, 1);
	} else if (!strcmp(argv[1], "press")) {
		int ms = argc > 2 ? atoi(argv[2]) : 200;

		gpio_emul_input_set_dt(&button, 0);
		k_msleep(ms);
		gpio_emul_input_set_dt(&button, 1);
	} else {
		return -EINVAL;
	}
	return 0;
}

static int cmd_gpio(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "lamp: %d", gpio_emul_output_get_dt(&lamp));
	shell_print(sh, "led: %d", gpio_emul_output_get_dt(&led));
	return 0;
}

static int cmd_sign(const struct shell *sh, size_t argc, char **argv)
{
	struct ws_frame f;
	struct ws_emul_sign_stats st;
	char row[WS_W + 1];

	ws_emul_sign_get(&f, &st);
	shell_print(sh, "frames: %u", st.frames);
	shell_print(sh, "errors: %u", st.errors);
	shell_print(sh, "bytes: %u", st.bytes);
	shell_print(sh, "last_ms: %lld", (long long)st.last_ms);
	for (int y = 0; y < WS_H; y++) {
		for (int x = 0; x < WS_W; x++) {
			row[x] = ws_frame_get(&f, x, y) ? '#' : '.';
		}
		row[WS_W] = '\0';
		shell_print(sh, "|%s|", row);
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	emul_cmds,
	SHELL_CMD_ARG(thp, NULL, "BME280: thp <t> <p_pa> <rh> | fail | ok", cmd_thp, 2, 2),
	SHELL_CMD_ARG(co2, NULL, "MH-Z19B: co2 <ppm> | bad | ok", cmd_co2, 2, 0),
	SHELL_CMD_ARG(button, NULL, "Button: down | up | press [ms]", cmd_button, 2, 1),
	SHELL_CMD(gpio, NULL, "Lamp and LED outputs", cmd_gpio),
	SHELL_CMD(sign, NULL, "Last frame seen on the sign line", cmd_sign), SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((ws), emul, &emul_cmds, "native_sim emulators", NULL, 0, 0);

/* The emulated pin reads 0 (= pressed, active low) until told otherwise:
 * release the button before gpio-keys and the io service look at it.
 * gpio-emul accepts an input level only on an input pin, and gpio-keys
 * configures it later, so configure it here first. */
static int button_release_at_boot(void)
{
	int ret = gpio_pin_configure_dt(&button, GPIO_INPUT);

	return ret ? ret : gpio_emul_input_set_dt(&button, 1);
}

SYS_INIT(button_release_at_boot, POST_KERNEL, 60);
