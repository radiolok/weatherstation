/*
 * Weatherstation firmware entry point.
 *
 * Services start from SYS_INIT or their own threads; main() only reports
 * the version.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

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
	return 0;
}
