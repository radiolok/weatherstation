/* STA <-> AP automaton on the scenarios of the plan. */
#include <zephyr/ztest.h>

#include <ws/netfsm.h>

static struct ws_netfsm f;

ZTEST(net, test_no_config_goes_to_ap)
{
	struct ws_net_out o = ws_netfsm_boot(&f, false, false, 0);

	zassert_equal(f.st, WS_NET_AP);
	zassert_true(o.act & WS_NETACT_SCAN);
	zassert_true(o.act & WS_NETACT_AP_START);
	zassert_equal(o.led, WS_LED_AP);
	/* 10 minutes without config: stays in AP */
	o = ws_netfsm_event(&f, WS_NETEV_TICK, false, 700);
	zassert_equal(f.st, WS_NET_AP);
	zassert_equal(o.act, 0);
}

ZTEST(net, test_button_at_boot)
{
	ws_netfsm_boot(&f, true, true, 0);
	zassert_equal(f.st, WS_NET_AP);
}

ZTEST(net, test_connect_and_lose_link_backoff)
{
	struct ws_net_out o = ws_netfsm_boot(&f, true, false, 0);

	zassert_equal(f.st, WS_NET_CONNECTING);
	zassert_true(o.act & WS_NETACT_CONNECT);
	zassert_equal(o.led, WS_LED_SLOW);
	o = ws_netfsm_event(&f, WS_NETEV_CONNECTED, true, 5);
	zassert_equal(f.st, WS_NET_ONLINE);
	zassert_true(o.act & WS_NETACT_ONLINE);
	zassert_equal(o.led, WS_LED_HEARTBEAT);

	/* link lost: reconnect after 1, 2, 4 ... 60 s */
	o = ws_netfsm_event(&f, WS_NETEV_DISCONNECTED, true, 100);
	zassert_true(o.act & WS_NETACT_OFFLINE);
	int64_t t = 100;
	const int expect[] = {1, 2, 4, 8, 16, 32, 60, 60};

	for (int i = 0; i < 8; i++) {
		o = ws_netfsm_event(&f, WS_NETEV_TICK, true, t + expect[i] - 1);
		zassert_equal(o.act, 0, "early connect at step %d", i);
		o = ws_netfsm_event(&f, WS_NETEV_TICK, true, t + expect[i]);
		zassert_true(o.act & WS_NETACT_CONNECT, "step %d", i);
		t += expect[i];
		o = ws_netfsm_event(&f, WS_NETEV_DISCONNECTED, true, t);
	}
	ws_netfsm_event(&f, WS_NETEV_TICK, true, t + 60);
	o = ws_netfsm_event(&f, WS_NETEV_CONNECTED, true, t + 61);
	zassert_equal(f.st, WS_NET_ONLINE);
	zassert_equal(f.backoff_s, WS_NET_BACKOFF_MIN_S);
}

ZTEST(net, test_long_press_and_ap_timeout)
{
	ws_netfsm_boot(&f, true, false, 0);
	ws_netfsm_event(&f, WS_NETEV_CONNECTED, true, 1);
	struct ws_net_out o = ws_netfsm_event(&f, WS_NETEV_BUTTON_LONG, true, 100);

	zassert_equal(f.st, WS_NET_AP);
	zassert_true(o.act & WS_NETACT_OFFLINE);
	zassert_true(o.act & WS_NETACT_DISCONNECT);
	zassert_true(o.act & WS_NETACT_SCAN);
	/* activity keeps the AP up */
	ws_netfsm_event(&f, WS_NETEV_ACTIVITY, true, 500);
	o = ws_netfsm_event(&f, WS_NETEV_TICK, true, 1000);
	zassert_equal(f.st, WS_NET_AP);
	/* 10 minutes without activity: back to the network */
	o = ws_netfsm_event(&f, WS_NETEV_TICK, true, 1100);
	zassert_equal(f.st, WS_NET_CONNECTING);
	zassert_true(o.act & WS_NETACT_AP_STOP);
	zassert_true(o.act & WS_NETACT_CONNECT);
}

ZTEST(net, test_saved_in_ap)
{
	ws_netfsm_boot(&f, false, false, 0);
	struct ws_net_out o = ws_netfsm_event(&f, WS_NETEV_SAVED, true, 30);

	zassert_equal(f.st, WS_NET_CONNECTING);
	zassert_true(o.act & WS_NETACT_AP_STOP);
	zassert_true(o.act & WS_NETACT_CONNECT);
	zassert_str_equal(ws_net_state_name(f.st), "connecting");
}

ZTEST_SUITE(net, NULL, NULL, NULL, NULL, NULL);
