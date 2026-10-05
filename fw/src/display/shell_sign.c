/* "ws sign" shell commands (F4, bench checks H3-H5). */
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>

#include "app.h"
#include "display.h"

static int cmd_pattern(const struct shell *sh, size_t argc, char **argv)
{
	int p = ws_display_pattern_parse(argv[1]);

	if (p < 0) {
		shell_error(sh, "patterns: checker columns rows all none auto");
		return -EINVAL;
	}
	ws_display_pattern(p);
	return 0;
}

static int cmd_show(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t minutes = argc > 2 ? strtoul(argv[2], NULL, 10) : 0;
	int s;

	ws_app_lock();
	s = ws_cfg_screen_by_id(ws_app_cfg(), argv[1]);
	ws_app_unlock();
	if (s < 0) {
		shell_error(sh, "no screen %s", argv[1]);
		return -EINVAL;
	}
	ws_display_pattern(WS_PATTERN_NONE);
	ws_display_pin(s, minutes);
	return 0;
}

static int cmd_auto(const struct shell *sh, size_t argc, char **argv)
{
	ws_display_pattern(WS_PATTERN_NONE);
	ws_display_pin(-1, 0);
	return 0;
}

static int cmd_stats(const struct shell *sh, size_t argc, char **argv)
{
	struct ws_display_stats st;

	ws_display_get_stats(&st);
	shell_print(sh, "frames: %u", st.frames_sent);
	shell_print(sh, "repeats: %u", st.frames_repeated);
	shell_print(sh, "flips_total: %u", st.flips_total);
	shell_print(sh, "flips_24h: %u", st.flips_24h);
	shell_print(sh, "last_dots: %u", st.last_dots);
	shell_print(sh, "last_cols: %u", st.last_cols);
	shell_print(sh, "tx_bytes: %u", st.tx_bytes);
	return 0;
}

static int cmd_frame(const struct shell *sh, size_t argc, char **argv)
{
	struct ws_frame f;
	char row[WS_W + 1];

	ws_display_last_frame(&f);
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
	sign_cmds,
	SHELL_CMD_ARG(pattern, NULL, "Test pattern: checker columns rows all none auto",
		      cmd_pattern, 2, 0),
	SHELL_CMD_ARG(show, NULL, "Pin a screen: show <id> [minutes]", cmd_show, 2, 1),
	SHELL_CMD(auto, NULL, "Back to the rules", cmd_auto),
	SHELL_CMD(stats, NULL, "Frame statistics", cmd_stats),
	SHELL_CMD(frame, NULL, "Last frame as text", cmd_frame), SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((ws), sign, &sign_cmds, "Sign", NULL, 0, 0);
