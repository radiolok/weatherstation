/* Task watchdog, see watchdog.h. */
#include <errno.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "watchdog.h"

LOG_MODULE_REGISTER(ws_wdt, LOG_LEVEL_INF);

#define MAX_CH CONFIG_TASK_WDT_CHANNELS

static const char *names[MAX_CH];
static bool started;

static void expired(int ch, void *user)
{
	const char *name = user ? user : "?";

	LOG_ERR("watchdog: thread '%s' hung, rebooting", name);
	LOG_PANIC();
	sys_reboot(SYS_REBOOT_COLD);
}

int ws_wdt_add(const char *name, uint32_t timeout_ms)
{
	if (!started) {
		return -EAGAIN;
	}
	int ch = task_wdt_add(timeout_ms, expired, (void *)name);

	if (ch < 0) {
		LOG_ERR("no channel for %s: %d", name, ch);
		return ch;
	}
	if (ch < MAX_CH) {
		names[ch] = name;
	}
	return ch;
}

void ws_wdt_feed(int ch)
{
	if (ch >= 0) {
		task_wdt_feed(ch);
	}
}

/* The system work queue runs the display and the clock ticker: a work item
 * that feeds a channel shows that it is not stuck. */
static int sysq_ch = -1;
static void sysq_feed(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(sysq_work, sysq_feed);

static void sysq_feed(struct k_work *w)
{
	ws_wdt_feed(sysq_ch);
	k_work_reschedule(&sysq_work, K_SECONDS(5));
}

int ws_watchdog_start(void)
{
	const struct device *hw = DEVICE_DT_GET_OR_NULL(DT_ALIAS(watchdog0));

	if (hw && !device_is_ready(hw)) {
		hw = NULL;
	}
	int ret = task_wdt_init(hw);

	if (ret) {
		LOG_ERR("task watchdog: %d", ret);
		return ret;
	}
	started = true;
	sysq_ch = ws_wdt_add("sysworkq", 30000);
	k_work_reschedule(&sysq_work, K_SECONDS(5));
	LOG_INF("task watchdog on%s", hw ? " (hardware fallback)" : "");
	return 0;
}

/* ws wdt hang <seconds> : the shell thread registers a channel with a 2 s
 * timeout and stops feeding it (bench test of the watchdog). */
static int cmd_hang(const struct shell *sh, size_t argc, char **argv)
{
	int ch = ws_wdt_add("shell-test", 2000);

	if (ch < 0) {
		shell_error(sh, "watchdog not running (%d)", ch);
		return -ENOEXEC;
	}
	shell_print(sh, "hanging the shell thread, expect a reboot in 2 s");
	k_sleep(K_SECONDS(argc > 1 ? atoi(argv[1]) : 10));
	shell_error(sh, "still alive: the watchdog did not fire");
	return -ENOEXEC;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "running: %s", started ? "yes" : "no");
	for (int i = 0; i < MAX_CH; i++) {
		if (names[i]) {
			shell_print(sh, "channel %d: %s", i, names[i]);
		}
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(wdt_cmds,
			       SHELL_CMD_ARG(status, NULL, "Watchdog channels", cmd_status, 1, 0),
			       SHELL_CMD_ARG(hang, NULL, "Test: hang the shell thread [s]",
					     cmd_hang, 1, 1),
			       SHELL_SUBCMD_SET_END);
SHELL_SUBCMD_ADD((ws), wdt, &wdt_cmds, "Task watchdog", NULL, 0, 0);
