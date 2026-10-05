/*
 * SNTP with two servers and a configurable interval (F6). The time base is
 * kept in app_state; local time comes from the POSIX TZ setting.
 */
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>
#include <zephyr/shell/shell.h>

#include "app.h"
#include "net_mgr.h"

LOG_MODULE_REGISTER(ws_time, LOG_LEVEL_INF);

#define RETRY_S 30

static K_SEM_DEFINE(sync_now, 0, 1);

static int try_server(const char *server)
{
	struct sntp_time ts;
	int r;

	if (!server || !server[0]) {
		return -EINVAL;
	}
	r = sntp_simple(server, 3000, &ts);
	if (r < 0) {
		LOG_WRN("SNTP %s: %d", server, r);
		return r;
	}
	ws_app_set_unix((int64_t)ts.seconds, server);
	LOG_INF("time from %s: %llu", server, (unsigned long long)ts.seconds);
	return 0;
}

static void time_thread(void *a, void *b, void *c)
{
	struct ws_settings s;

	for (;;) {
		ws_net_wait_online(K_FOREVER);
		ws_app_settings_get(&s);
		const char *first = s.ntp1[0] ? s.ntp1 : CONFIG_WS_NTP_DEFAULT_SERVER;

		if (!first[0] && !s.ntp2[0]) {
			/* no server at all (native_sim tests): wait for a setting */
			k_sem_take(&sync_now, K_FOREVER);
			continue;
		}
		int r = first[0] ? try_server(first) : -ENOENT;

		if (r < 0) {
			r = try_server(s.ntp2);
		}
		uint32_t wait = r < 0 ? RETRY_S : (uint32_t)s.ntp_interval * 60;

		k_sem_take(&sync_now, K_SECONDS(wait));
	}
}

K_THREAD_STACK_DEFINE(time_stack, 3072);
static struct k_thread time_tid;

int ws_timesync_start(void)
{
	k_thread_create(&time_tid, time_stack, K_THREAD_STACK_SIZEOF(time_stack), time_thread, NULL,
			NULL, NULL, 9, 0, K_NO_WAIT);
	k_thread_name_set(&time_tid, "ws_time");
	return 0;
}

/* NTP settings changed: sync again */
static void on_settings(const struct zbus_channel *chan)
{
	k_sem_give(&sync_now);
}

ZBUS_LISTENER_DEFINE(ws_time_set_lis, on_settings);
ZBUS_CHAN_ADD_OBS(ws_chan_settings, ws_time_set_lis, 4);

static int cmd_ntp(const struct shell *sh, size_t argc, char **argv)
{
	k_sem_give(&sync_now);
	shell_print(sh, "synced: %s", ws_app_time_synced() ? "yes" : "no");
	shell_print(sh, "server: %s", ws_app_time_server());
	shell_print(sh, "last: %lld", (long long)ws_app_last_sync());
	return 0;
}

SHELL_SUBCMD_ADD((ws), ntp, NULL, "Sync the clock now and show the state", cmd_ntp, 1, 0);
