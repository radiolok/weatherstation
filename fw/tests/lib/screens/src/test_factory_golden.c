/* Factory screens: the C renderer against frames drawn by fw/web/src/render.js. */
#include <string.h>
#include <zephyr/ztest.h>

#include "common.h"
#include "screens_golden_gen.h"

#define N(a) (int)(sizeof(a) / sizeof((a)[0]))

static void load_set(const struct gs_set *s)
{
	ws_vars_init(&vars);
	for (int i = 0; i < s->n; i++) {
		int id = ws_var_lookup(s->vars[i].name);

		zassert_true(id >= 0, "%s", s->vars[i].name);
		if (s->vars[i].str) {
			ws_vars_set_str(&vars, id, s->vars[i].str, 0);
		} else {
			ws_vars_set_num(&vars, id, s->vars[i].num, 0);
		}
	}
	struct ws_hours h = {0};

	for (int i = 0; i < s->n_hours && i < WS_HOURS_MAX; i++) {
		h.t[i] = s->hours[i][0];
		h.pop[i] = (uint8_t)s->hours[i][1];
		h.n++;
	}
	if (h.n) {
		ws_vars_set_hours(&vars, &h, 0);
	}
}

ZTEST(screens, test_factory_screens_match_web_renderer)
{
	struct ws_frame f;
	int bad = 0;

	for (int i = 0; i < N(gs_frames); i++) {
		const struct gs_frame *g = &gs_frames[i];

		zassert_ok(compile_factory()); /* fresh hysteresis state */
		load_set(&gs_sets[g->set]);
		/* the golden values are "as reported": fallback and ages are not
		 * recomputed, make the forecast look fresh */
		vars.fc_rx = 0;

		struct ws_render_ctx rc = {&cfg, &vars, {0, 0}, g->sod};
		int s = screen_id(g->screen);

		zassert_true(s >= 0);
		ws_render_screen(&rc, s, &f);
		if (memcmp(f.bits, g->frame, sizeof(f.bits))) {
			TC_PRINT("set %s screen %s sod %d\n", gs_sets[g->set].name, g->screen,
				 (int)g->sod);
			dump_frame_diff(&f, g->frame);
			bad++;
		}
	}
	zassert_equal(bad, 0, "%d frames differ", bad);
}
