/* Table tests of the selection rules (spec, section 6). */
#include <string.h>
#include <zephyr/ztest.h>

#include "common.h"

static struct ws_engine e;
static int64_t T;

#define MAIN "{\"id\":\"main\",\"default\":true,\"items\":[]}"

static void boot(const char *json)
{
	zassert_ok(compile_cfg(json));
	ws_vars_init(&vars);
	T = 1000;
	set_clock(T, 12, 0, 3);
	ws_engine_init(&e, &cfg, T);
}

/* Advances time in 10 s steps (the display service period). */
static void run(int seconds)
{
	struct ws_now now;

	for (int s = 0; s < seconds; s += 10) {
		T += 10;
		now = (struct ws_now){T, 0};
		ws_engine_step(&e, &vars, &now);
	}
}

static const char *cur(void)
{
	return cfg.screens[e.current].id;
}

/* A fresh dry forecast so that "noforecast" stays away */
static void forecast(void)
{
	const char *fc = "{\"now\":{\"t\":5,\"cond\":\"cloudy\"},\"rain\":null}";

	ws_forecast_apply(&vars, fc, strlen(fc), T);
}

static void co2(int ppm)
{
	ws_vars_set_num(&vars, WS_V_IN_CO2, ppm * 10, T);
}

ZTEST(screens, test_hysteresis_gt)
{
	boot("{\"schema\":1,\"screens\":[" MAIN ",{\"id\":\"co2\",\"rule\":{\"all\":[{\"var\":"
	     "\"in.co2\",\"op\":\">\",\"val\":1000,\"hyst\":100}],\"min_show\":0},\"items\":[]}]}");
	co2(990);
	run(10);
	zassert_str_equal(cur(), "main");
	co2(1001);
	run(10);
	zassert_str_equal(cur(), "co2");
	zassert_not_null(strstr(e.reason, "in.co2=1001 > 1000"), "%s", e.reason);
	co2(950); /* above 1000 - 100: stays */
	run(10);
	zassert_str_equal(cur(), "co2");
	co2(899);
	run(10);
	zassert_str_equal(cur(), "main");
	co2(950); /* below 1000 again: needs > 1000 to come back */
	run(10);
	zassert_str_equal(cur(), "main");
}

ZTEST(screens, test_co2_jitter_does_not_flip)
{
	/* the factory "stuffy" screen: CO2 980..1020 must not blink */
	zassert_ok(compile_factory());
	ws_vars_init(&vars);
	T = 1000;
	set_clock(T, 12, 0, 3);
	ws_engine_init(&e, &cfg, T);
	forecast();
	int changes = 0;
	struct ws_now now;

	for (int i = 0; i < 360; i++) { /* one hour */
		co2(i % 2 ? 980 : 1020);
		T += 10;
		now = (struct ws_now){T, 0};
		changes += ws_engine_step(&e, &vars, &now);
	}
	/* on_delay 120 s holds while the value jumps across 1000, the
	 * hysteresis keeps it: at most one switch to "stuffy" */
	zassert_true(changes <= 1, "changes %d", changes);
}

ZTEST(screens, test_on_off_delay_and_min_show)
{
	boot("{\"schema\":1,\"screens\":[" MAIN ",{\"id\":\"x\",\"rule\":{\"all\":[{\"var\":"
	     "\"in.co2\",\"op\":\">\",\"val\":1000}],\"on_delay\":30,\"off_delay\":20,"
	     "\"min_show\":60},\"items\":[]}]}");
	co2(1500);
	run(20);
	zassert_str_equal(cur(), "main"); /* on_delay 30 s */
	run(20);
	zassert_str_equal(cur(), "x");
	co2(500);
	run(20);
	zassert_str_equal(cur(), "x"); /* off_delay 20 s */
	run(20);
	zassert_str_equal(cur(), "x"); /* min_show 60 s since switching */
	run(30);
	zassert_str_equal(cur(), "main");
	zassert_str_equal(e.reason, "по умолчанию");
}

ZTEST(screens, test_priorities_and_ties)
{
	boot("{\"schema\":1,\"screens\":[" MAIN
	     ",{\"id\":\"lo\",\"rule\":{\"all\":[{\"var\":\"in.t\",\"op\":\">\",\"val\":20}],"
	     "\"prio\":30,\"min_show\":300},\"items\":[]},"
	     "{\"id\":\"a\",\"rule\":{\"all\":[{\"var\":\"in.co2\",\"op\":\">\",\"val\":1000}],"
	     "\"prio\":60,\"min_show\":0},\"items\":[]},"
	     "{\"id\":\"b\",\"rule\":{\"all\":[{\"var\":\"in.co2\",\"op\":\">\",\"val\":1000}],"
	     "\"prio\":60,\"min_show\":0},\"items\":[]}]}");
	ws_vars_set_num(&vars, WS_V_IN_T, 250, T);
	run(10);
	zassert_str_equal(cur(), "lo");
	co2(1200);
	run(10);
	/* higher priority switches at once, min_show of "lo" does not matter;
	 * equal priorities: the one higher in the list wins */
	zassert_str_equal(cur(), "a");
	co2(500);
	run(10);
	/* "lo" is still active: back to it */
	zassert_str_equal(cur(), "lo");
	ws_vars_set_num(&vars, WS_V_IN_T, 150, T);
	run(10);
	zassert_str_equal(cur(), "lo"); /* min_show 300 s */
	run(300);
	zassert_str_equal(cur(), "main");
}

ZTEST(screens, test_time_window_through_midnight)
{
	struct ws_rule r = {.present = true,
			    .has_time = true,
			    .from_min = 22 * 60,
			    .to_min = 7 * 60,
			    .days = 0x7F};

	zassert_true(ws_rule_time_ok(&r, 23 * 60, 1));
	zassert_true(ws_rule_time_ok(&r, 0, 1));
	zassert_true(ws_rule_time_ok(&r, 6 * 60 + 59, 1));
	zassert_false(ws_rule_time_ok(&r, 7 * 60, 1));
	zassert_false(ws_rule_time_ok(&r, 12 * 60, 1));
	zassert_false(ws_rule_time_ok(&r, -1, 1)); /* clock unknown */
	r.days = 1u << 5;                          /* Saturday only */
	zassert_false(ws_rule_time_ok(&r, 23 * 60, 1));
	zassert_true(ws_rule_time_ok(&r, 23 * 60, 6));
	r.from_min = r.to_min = 0;
	r.days = 0x7F;
	zassert_true(ws_rule_time_ok(&r, 600, 2));
}

ZTEST(screens, test_unknown_is_false_and_no_clock)
{
	boot("{\"schema\":1,\"screens\":[" MAIN
	     ",{\"id\":\"n\",\"rule\":{\"all\":[{\"var\":\"in.co2\",\"op\":\"<\",\"val\":5000}],"
	     "\"min_show\":0},\"items\":[]},"
	     "{\"id\":\"t\",\"rule\":{\"time\":{\"from\":\"00:00\",\"to\":\"23:59\"},"
	     "\"min_show\":0},\"items\":[]}]}");
	ws_vars_clear(&vars, WS_V_TIME_HOUR);
	run(10);
	zassert_str_equal(cur(), "main"); /* co2 unknown, clock unknown */
	set_clock(T, 10, 0, 1);
	run(10);
	zassert_str_equal(cur(), "t");
}

ZTEST(screens, test_insert_mode)
{
	boot("{\"schema\":1,\"screens\":[" MAIN ",{\"id\":\"i\",\"rule\":{\"all\":[{\"var\":"
	     "\"fc.ice\",\"op\":\"=\",\"val\":true}],\"mode\":\"insert\",\"insert\":{\"show\":"
	     "30,\"every\":5}},\"items\":[]}]}");
	ws_vars_set_num(&vars, WS_V_FC_ICE, 1, T);
	int shown = 0;
	struct ws_now now;

	for (int i = 0; i < 60; i++) { /* 10 minutes */
		T += 10;
		ws_vars_set_num(&vars, WS_V_FC_ICE, 1, T);
		now = (struct ws_now){T, 0};
		ws_engine_step(&e, &vars, &now);
		shown += !strcmp(cur(), "i");
	}
	/* 30 s of every 5 min: 2 windows of 3 steps */
	zassert_equal(shown, 6, "shown %d", shown);
}

ZTEST(screens, test_pin_and_debug_button)
{
	zassert_ok(compile_factory());
	ws_vars_init(&vars);
	T = 1000;
	set_clock(T, 12, 0, 3);
	ws_engine_init(&e, &cfg, T);
	forecast();
	run(10);
	zassert_str_equal(cur(), "main");
	ws_engine_pin(&e, screen_id("evening"), 1, T);
	run(10);
	zassert_str_equal(cur(), "evening");
	zassert_str_equal(e.reason, "закреплён вручную");
	run(60);
	zassert_str_equal(cur(), "main"); /* pin for 1 min ended */

	/* short press: next in the list, disabled screens too, 60 s */
	cfg.screens[screen_id("rain")].enabled = false;
	ws_engine_debug_next(&e, T);
	run(10);
	zassert_equal(e.current, (cfg.def + 1) % cfg.n_screens);
	ws_engine_debug_next(&e, T);
	run(10);
	zassert_equal(e.current, (cfg.def + 2) % cfg.n_screens);
	zassert_str_equal(e.reason, "кнопка");
	run(60);
	zassert_str_equal(cur(), "main");

	ws_engine_pin(&e, screen_id("stuffy"), 0, T);
	run(3600);
	zassert_str_equal(cur(), "stuffy"); /* until cancelled */
	ws_engine_pin(&e, -1, 0, T);
	run(10);
	zassert_str_equal(cur(), "main");
}

ZTEST(screens, test_reconfigure_keeps_current)
{
	zassert_ok(compile_factory());
	ws_vars_init(&vars);
	T = 1000;
	set_clock(T, 23, 30, 3);
	ws_engine_init(&e, &cfg, T);
	run(10);
	zassert_str_equal(cur(), "evening");
	static struct ws_config cfg_b;

	memcpy(&cfg_b, &cfg, sizeof(cfg));
	ws_engine_reconfigure(&e, &cfg_b, T);
	zassert_str_equal(cfg_b.screens[e.current].id, "evening");
}
