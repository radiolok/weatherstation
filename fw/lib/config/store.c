/* Configuration load order, see ws_cfg_load_chain(). */
#include <stdio.h>

#include <ws/config.h>

int ws_cfg_load_chain(const struct ws_cfg_source *srcs, int n, char *buf, size_t cap,
		      struct ws_config *out, struct ws_cfg_errors *errs, struct ws_jtok *toks,
		      int max_toks)
{
	for (int i = 0; i < n; i++) {
		int len = srcs[i].read(srcs[i].ctx, buf, cap);

		if (len <= 0) {
			errs->count = 1;
			snprintf(errs->e[0].path, sizeof(errs->e[0].path), "%s", srcs[i].name);
			snprintf(errs->e[0].msg, sizeof(errs->e[0].msg), "не читается (%d)", len);
			continue;
		}
		if (ws_cfg_compile(buf, (size_t)len, out, errs, toks, max_toks) == 0) {
			return i;
		}
	}
	return -1;
}
