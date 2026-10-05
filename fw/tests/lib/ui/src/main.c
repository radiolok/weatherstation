/* Service button and LED automata. */
#include <zephyr/ztest.h>

#include <ws/ui.h>

ZTEST(ui, test_short_press)
{
	struct ws_button b;

	ws_button_init(&b, false, 0);
	zassert_equal(ws_button_edge(&b, true, 1000), WS_BTN_NONE);
	zassert_equal(ws_button_poll(&b, 1100), WS_BTN_NONE);
	zassert_equal(ws_button_edge(&b, false, 1200), WS_BTN_SHORT);
}

ZTEST(ui, test_glitch_and_bounce)
{
	struct ws_button b;

	ws_button_init(&b, false, 0);
	ws_button_edge(&b, true, 1000);
	zassert_equal(ws_button_edge(&b, false, 1010), WS_BTN_NONE); /* 10 ms glitch */
	/* repeated edges with the same level are ignored */
	ws_button_edge(&b, true, 2000);
	zassert_equal(ws_button_edge(&b, true, 2005), WS_BTN_NONE);
	zassert_equal(ws_button_edge(&b, false, 2300), WS_BTN_SHORT);
	zassert_equal(ws_button_edge(&b, false, 2310), WS_BTN_NONE);
}

ZTEST(ui, test_long_press_once)
{
	struct ws_button b;

	ws_button_init(&b, false, 0);
	ws_button_edge(&b, true, 1000);
	zassert_equal(ws_button_poll(&b, 5900), WS_BTN_NONE);
	zassert_equal(ws_button_poll(&b, 6000), WS_BTN_LONG);
	zassert_equal(ws_button_poll(&b, 7000), WS_BTN_NONE);
	/* release after a long press is not a short press */
	zassert_equal(ws_button_edge(&b, false, 9000), WS_BTN_NONE);
}

ZTEST(ui, test_held_at_boot)
{
	struct ws_button b;

	ws_button_init(&b, true, 0);
	zassert_equal(ws_button_poll(&b, 100), WS_BTN_BOOT_HELD);
	zassert_equal(ws_button_poll(&b, 6000), WS_BTN_NONE); /* no extra long press */
	zassert_equal(ws_button_edge(&b, false, 7000), WS_BTN_NONE);
	/* afterwards it works normally */
	ws_button_edge(&b, true, 8000);
	zassert_equal(ws_button_edge(&b, false, 8200), WS_BTN_SHORT);
}

ZTEST(ui, test_led_patterns)
{
	zassert_false(ws_led_level(WS_LED_OFF, 0));
	zassert_true(ws_led_level(WS_LED_ON, 12345));
	zassert_true(ws_led_level(WS_LED_HEARTBEAT, 4010));
	zassert_false(ws_led_level(WS_LED_HEARTBEAT, 4100));
	zassert_true(ws_led_level(WS_LED_AP, 260));
	zassert_false(ws_led_level(WS_LED_AP, 200));
	int on = 0;

	for (int t = 0; t < 1000; t++) {
		on += ws_led_level(WS_LED_SLOW, t);
	}
	zassert_equal(on, 500);
	zassert_equal(ws_led_parse("ap"), WS_LED_AP);
	zassert_equal(ws_led_parse("disco"), -1);
	zassert_str_equal(ws_led_name(WS_LED_FAST), "fast");
}

ZTEST_SUITE(ui, NULL, NULL, NULL, NULL, NULL);
