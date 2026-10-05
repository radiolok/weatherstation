/*
 * Host helper for the web UI mock server (fw/tests/web/mock_server.py): the
 * same configuration compiler, catalogue and variable JSON as the firmware,
 * so Playwright tests run without a Zephyr build.
 *
 *   wsapi catalog  < screens.json   -> catalogue (user pictograms included)
 *   wsapi validate < screens.json   -> {"ok":true} or {"ok":false,"errors":[...]}, exit 1
 *   wsapi canon    < screens.json   -> canonical JSON of the compiled file
 *   wsapi vars     < {"in.co2":850} -> variables as /api/vars returns them
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ws/config.h>
#include <ws/json.h>
#include <ws/vars.h>

static char in[WS_JSON_MAX_LEN + 1];
static char out[128 * 1024];
static struct ws_jtok toks[WS_JSON_TOKENS];
static struct ws_config cfg;
static struct ws_cfg_errors errs;

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: wsapi catalog|validate|canon|vars < json\n");
		return 2;
	}
	size_t n = fread(in, 1, sizeof(in) - 1, stdin);
	const char *cmd = argv[1];

	if (!strcmp(cmd, "vars")) {
		struct ws_vars vars;
		struct ws_json j;
		struct ws_now now = {.mono = 1000, .unix_s = 1760000000};

		ws_vars_init(&vars);
		if (n && ws_json_parse(&j, in, n, toks, WS_JSON_TOKENS) > 0) {
			ws_vars_apply_json(&vars, &j, 0, now.mono);
		}
		if (ws_vars_to_json(&vars, &now, out, sizeof(out)) < 0) {
			return 3;
		}
		puts(out);
		return 0;
	}
	int rc = ws_cfg_compile(in, n, &cfg, &errs, toks, WS_JSON_TOKENS);

	if (!strcmp(cmd, "catalog")) {
		if (ws_catalog_json(rc ? NULL : &cfg, out, sizeof(out)) < 0) {
			return 3;
		}
		puts(out);
		return 0;
	}
	if (rc) {
		struct ws_jw w;

		ws_jw_init(&w, out, sizeof(out));
		ws_jw_obj(&w);
		ws_jw_kbool(&w, "ok", false);
		ws_jw_key(&w, "errors");
		ws_cfg_errors_json(&errs, &w);
		ws_jw_obj_end(&w);
		puts(out);
		return 1;
	}
	if (!strcmp(cmd, "canon")) {
		if (ws_cfg_to_json(&cfg, out, sizeof(out)) < 0) {
			return 3;
		}
		puts(out);
		return 0;
	}
	puts("{\"ok\":true}");
	return 0;
}
