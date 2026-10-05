/*
 * Weatherstation firmware entry point: starts the services in order.
 * Architecture: fw/README.md, plan: fw/docs/implementation-plan.md.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include "app.h"
#include "display/display.h"
#include "io/io.h"
#include "services.h"
#include "storage/storage.h"

LOG_MODULE_REGISTER(ws_main, LOG_LEVEL_INF);

static int cmd_version(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "version: %s", CONFIG_WS_VERSION);
	shell_print(sh, "board: %s", CONFIG_BOARD_TARGET);
	return 0;
}

/* Other modules add their commands with SHELL_SUBCMD_ADD((ws), ...). */
SHELL_SUBCMD_SET_CREATE(ws_cmds, (ws));
SHELL_SUBCMD_ADD((ws), version, NULL, "Firmware version", cmd_version, 1, 0);
SHELL_CMD_REGISTER(ws, &ws_cmds, "Weatherstation commands", NULL);

int main(void)
{
	printk("weatherstation %s\n", CONFIG_WS_VERSION);

	ws_storage_init();
	ws_settings_init();
	ws_app_state_init();
	LOG_INF("device id %s", ws_app_device_id());
	ws_app_clock_start();
	ws_watchdog_start();

	ws_io_start();
	ws_display_start();
	ws_ota_boot_check();
	ws_net_start(ws_io_button_held_at_boot());
	ws_mqtt_start();
	ws_web_start();
	ws_metar_start();
	return 0;
}
