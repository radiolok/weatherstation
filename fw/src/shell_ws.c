/*
 * "ws" shell commands for the bench and for the native_sim tests:
 * variables, time, screen configuration and settings.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include <ws/json.h>
#include <ws/sign.h>

#include "app.h"
#include "storage/storage.h"

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct ws_msg_net net;
	struct ws_msg_mqtt mq;
	struct ws_tm tm;
	struct ws_now now = ws_app_now();

	zbus_chan_read(&ws_chan_net, &net, K_MSEC(10));
	zbus_chan_read(&ws_chan_mqtt, &mq, K_MSEC(10));
	shell_print(sh, "id: %s", ws_app_device_id());
	shell_print(sh, "uptime: %lld", (long long)now.mono);
	shell_print(sh, "net: %d", net.status);
	shell_print(sh, "ip: %s", net.ip);
	shell_print(sh, "mqtt: %s", mq.connected ? "connected" : "disconnected");
	if (ws_app_localtime(&tm)) {
		shell_print(sh, "time: %04d-%02d-%02d %02d:%02d:%02d", tm.year, tm.month, tm.day,
			    tm.hour, tm.min, tm.sec);
		shell_print(sh, "ntp: %s", ws_app_time_server());
	} else {
		shell_print(sh, "time: unknown");
	}
	ws_app_lock();
	struct ws_engine *e = ws_app_engine();
	struct ws_config *c = ws_app_cfg();

	shell_print(sh, "screens: %u (%s)", c->n_screens, ws_app_cfg_source());
	shell_print(sh, "screen: %s", c->screens[e->current].id);
	shell_print(sh, "reason: %s", e->reason);
	shell_print(sh, "pinned: %s", e->pinned >= 0 ? c->screens[e->pinned].id : "no");
	ws_app_unlock();
	return 0;
}

static int cmd_vars(const struct shell *sh, size_t argc, char **argv)
{
	struct ws_now now = ws_app_now();
	char v[32];

	ws_app_lock();
	for (int i = 0; i < WS_V_COUNT; i++) {
		struct ws_value x;

		ws_vars_get(ws_app_vars(), i, &now, &x);
		ws_value_format(&x, x.type, v, sizeof(v));
		shell_print(sh, "%s: %s %s", ws_var_name(i), v, ws_vsrc_name(x.src));
	}
	ws_app_unlock();
	return 0;
}

/* ws var set <name> <value> */
static int cmd_var_set(const struct shell *sh, size_t argc, char **argv)
{
	int id = ws_var_lookup(argv[1]);
	const char *val = argv[2];

	if (id < 0) {
		shell_error(sh, "unknown variable %s", argv[1]);
		return -EINVAL;
	}
	const struct ws_var_info *inf = ws_var_info(id);
	int32_t num;

	switch (inf->type) {
	case WS_VT_BOOL:
		num = !strcmp(val, "true") || !strcmp(val, "1");
		break;
	case WS_VT_COND:
		num = ws_cond_parse(val);
		break;
	case WS_VT_DIR:
		num = ws_dir_parse(val);
		break;
	case WS_VT_STR:
		ws_app_set_str(id, val);
		return 0;
	case WS_VT_HOURS:
		shell_error(sh, "use the forecast to set fc.hours");
		return -EINVAL;
	default: {
		/* decimal number into tenths */
		char *end;
		double d = strtod(val, &end);

		if (*end) {
			if (inf->type == WS_VT_ANY) {
				ws_app_set_str(id, val);
				return 0;
			}
			shell_error(sh, "not a number: %s", val);
			return -EINVAL;
		}
		num = (int32_t)(d >= 0 ? d * 10 + 0.5 : d * 10 - 0.5);
		break;
	}
	}
	if (num < 0 && (inf->type == WS_VT_COND || inf->type == WS_VT_DIR)) {
		shell_error(sh, "bad value %s", val);
		return -EINVAL;
	}
	ws_app_set_num(id, num);
	return 0;
}

static int cmd_var_clear(const struct shell *sh, size_t argc, char **argv)
{
	int id = ws_var_lookup(argv[1]);

	if (id < 0) {
		shell_error(sh, "unknown variable %s", argv[1]);
		return -EINVAL;
	}
	ws_app_clear(id);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	var_cmds, SHELL_CMD_ARG(set, NULL, "Set a variable: set <name> <value>", cmd_var_set, 3, 0),
	SHELL_CMD_ARG(clear, NULL, "Make a variable unknown: clear <name>", cmd_var_clear, 2, 0),
	SHELL_SUBCMD_SET_END);

/* ws forecast <json...> (the words are joined with spaces) */
static int cmd_forecast(const struct shell *sh, size_t argc, char **argv)
{
	static char buf[CONFIG_SHELL_CMD_BUFF_SIZE];
	size_t o = 0;
	struct ws_now now = ws_app_now();

	for (size_t i = 1; i < argc && o < sizeof(buf) - 2; i++) {
		o += snprintf(buf + o, sizeof(buf) - o, "%s%s", i > 1 ? " " : "", argv[i]);
	}
	ws_app_lock();
	int r = ws_forecast_apply(ws_app_vars(), buf, o, now.mono);

	ws_app_unlock();
	ws_app_vars_touched();
	if (r) {
		shell_error(sh, "bad forecast JSON");
	}
	return r;
}

/* ws time set <unix> : sets the wall clock like an NTP sync would */
static int cmd_time_set(const struct shell *sh, size_t argc, char **argv)
{
	int64_t t = strtoll(argv[1], NULL, 10);

	if (t < 1000000000LL) {
		shell_error(sh, "unix time expected");
		return -EINVAL;
	}
	ws_app_set_unix(t, "shell");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(time_cmds,
			       SHELL_CMD_ARG(set, NULL, "Set the clock: set <unix time>",
					     cmd_time_set, 2, 0),
			       SHELL_SUBCMD_SET_END);

static int cmd_cfg_status(const struct shell *sh, size_t argc, char **argv)
{
	ws_app_lock();
	shell_print(sh, "source: %s", ws_app_cfg_source());
	shell_print(sh, "screens: %u", ws_app_cfg()->n_screens);
	ws_app_unlock();
	return 0;
}

static void print_errors(const struct shell *sh, const struct ws_cfg_errors *e)
{
	for (int i = 0; i < e->count && i < WS_MAX_ERRORS; i++) {
		shell_error(sh, "%s: %s", e->e[i].path, e->e[i].msg);
	}
}

static int cmd_cfg_factory(const struct shell *sh, size_t argc, char **argv)
{
	static struct ws_cfg_errors e;
	int r = ws_app_cfg_apply((const char *)ws_factory_screens_json, ws_factory_screens_json_len,
				 true, &e);

	if (r) {
		print_errors(sh, &e);
	}
	return r;
}

static int cmd_cfg_rollback(const struct shell *sh, size_t argc, char **argv)
{
	static struct ws_cfg_errors e;
	int r = ws_app_cfg_rollback(&e);

	if (r) {
		print_errors(sh, &e);
	}
	return r;
}

/* Test helper: writes a truncated screens.json (and optionally the previous
 * version) to check the start-up fallback on the flash simulator. */
static int cmd_cfg_corrupt(const struct shell *sh, size_t argc, char **argv)
{
	static const char broken[] = "{\"schema\":1,\"screens\":[{\"id\":\"x\",";
	int r = ws_storage_write_raw(WS_CFG_CURRENT, broken, sizeof(broken) - 1);

	if (r == 0 && argc > 1 && !strcmp(argv[1], "both")) {
		r = ws_storage_write_raw(WS_CFG_PREVIOUS, broken, sizeof(broken) - 1);
	}
	shell_print(sh, "corrupted: %d", r);
	return r;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	cfg_cmds, SHELL_CMD(status, NULL, "Where the screens came from", cmd_cfg_status),
	SHELL_CMD(factory, NULL, "Save and apply the factory set", cmd_cfg_factory),
	SHELL_CMD(rollback, NULL, "Back to the previous version", cmd_cfg_rollback),
	SHELL_CMD_ARG(corrupt, NULL, "Test: break screens.json [both]", cmd_cfg_corrupt, 1, 1),
	SHELL_SUBCMD_SET_END);

/* ws set <key> <value> : one setting; numbers stay numbers */
static int cmd_set(const struct shell *sh, size_t argc, char **argv)
{
	static char json[CONFIG_SHELL_CMD_BUFF_SIZE + 64];
	static struct ws_cfg_errors e;
	const struct ws_setting_field *f = ws_setting_find(argv[1]);
	struct ws_jw w;

	ws_jw_init(&w, json, sizeof(json));
	ws_jw_obj(&w);
	if (f && f->type == WS_SET_INT) {
		ws_jw_kint(&w, argv[1], strtol(argv[2], NULL, 10));
	} else {
		ws_jw_kstr(&w, argv[1], argv[2]);
	}
	ws_jw_obj_end(&w);
	int r = ws_app_settings_apply(json, w.pos, &e);

	if (r) {
		print_errors(sh, &e);
	}
	return r;
}

static int cmd_settings(const struct shell *sh, size_t argc, char **argv)
{
	static char buf[4096];
	struct ws_settings s;

	ws_app_settings_get(&s);
	if (ws_settings_to_json(&s, buf, sizeof(buf)) > 0) {
		shell_print(sh, "%s", buf);
	}
	return 0;
}

SHELL_SUBCMD_ADD((ws), status, NULL, "Device status", cmd_status, 1, 0);
SHELL_SUBCMD_ADD((ws), vars, NULL, "All variables", cmd_vars, 1, 0);
SHELL_SUBCMD_ADD((ws), var, &var_cmds, "Set or clear a variable", NULL, 0, 0);
SHELL_SUBCMD_ADD((ws), forecast, NULL, "Apply a forecast JSON", cmd_forecast, 2, 0);
SHELL_SUBCMD_ADD((ws), time, &time_cmds, "Wall clock", NULL, 0, 0);
SHELL_SUBCMD_ADD((ws), cfg, &cfg_cmds, "Screen configuration", NULL, 0, 0);
SHELL_SUBCMD_ADD((ws), set, NULL, "Change a setting: set <key> <value>", cmd_set, 3, 0);
SHELL_SUBCMD_ADD((ws), settings, NULL, "Show settings", cmd_settings, 1, 0);
