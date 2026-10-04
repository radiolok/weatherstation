/* JSON tokenizer and writer. */
#include <string.h>
#include <zephyr/ztest.h>

#include <ws/json.h>

static struct ws_jtok toks[256];

ZTEST(json, test_parse_walk)
{
	const char *s = "{\"a\": [1, 2.5, -3e1], \"b\": {\"c\": \"x\\\"y\\u0416\"}, \"d\": true,"
			" \"e\": null}";
	struct ws_json j;

	zassert_true(ws_json_parse(&j, s, strlen(s), toks, 256) > 0);
	int a = ws_json_get(&j, 0, "a");

	zassert_true(ws_json_is(&j, a, WS_J_ARR));
	zassert_equal(j.t[a].size, 3);
	int32_t v;

	zassert_true(ws_json_int(&j, ws_json_at(&j, a, 0), &v));
	zassert_equal(v, 1);
	zassert_false(ws_json_int(&j, ws_json_at(&j, a, 1), &v));
	zassert_true(ws_json_deci(&j, ws_json_at(&j, a, 1), &v));
	zassert_equal(v, 25);
	zassert_true(ws_json_deci(&j, ws_json_at(&j, a, 2), &v));
	zassert_equal(v, -300);
	zassert_equal(ws_json_at(&j, a, 3), -1);

	char buf[16];
	int c = ws_json_get(&j, ws_json_get(&j, 0, "b"), "c");

	zassert_equal(ws_json_str(&j, c, buf, sizeof(buf)), 5);
	zassert_str_equal(buf, "x\"y\xD0\x96");
	zassert_equal(ws_json_str(&j, c, buf, 3), -1);
	bool b = false;

	zassert_true(ws_json_bool(&j, ws_json_get(&j, 0, "d"), &b));
	zassert_true(b);
	zassert_true(ws_json_is(&j, ws_json_get(&j, 0, "e"), WS_J_NULL));
	zassert_equal(ws_json_get(&j, 0, "zz"), -1);

	int keys = 0;

	for (int k = ws_json_first(&j, 0); k >= 0; k = ws_json_next(&j, 0, k)) {
		keys++;
	}
	zassert_equal(keys, 4);
}

ZTEST(json, test_deci_rounding)
{
	const char *cases[] = {"12.34", "12.35", "-12.35", "0.04", "-0.05", "748", "1e2"};
	const int32_t want[] = {123, 124, -124, 0, -1, 7480, 1000};
	struct ws_json j;

	for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
		int32_t v;

		zassert_true(ws_json_parse(&j, cases[i], strlen(cases[i]), toks, 256) > 0);
		zassert_true(ws_json_deci(&j, 0, &v), "%s", cases[i]);
		zassert_equal(v, want[i], "%s", cases[i]);
	}
}

ZTEST(json, test_errors)
{
	struct ws_json j;
	const char *bad[] = {"{\"a\":", "[1,2",    "{\"a\" 1}", "[1,]",      "tru",
			     "{} x",    "\"\\q\"", "[01x]",     "{\"a\":1,}"};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		zassert_true(ws_json_parse(&j, bad[i], strlen(bad[i]), toks, 256) < 0, "%s",
			     bad[i]);
	}
	zassert_equal(ws_json_parse(&j, "{\"a\":", 5, toks, 256), WS_JSON_EPART);
	zassert_equal(ws_json_parse(&j, "[1,2,3]", 7, toks, 3), WS_JSON_ENOMEM);
	/* nesting limit */
	char deep[64];

	memset(deep, '[', 40);
	memset(deep + 40, ']', 20);
	zassert_true(ws_json_parse(&j, deep, 60, toks, 256) < 0);
}

ZTEST(json, test_writer)
{
	char buf[128];
	struct ws_jw w;

	ws_jw_init(&w, buf, sizeof(buf));
	ws_jw_obj(&w);
	ws_jw_kstr(&w, "s", "a\"b\n");
	ws_jw_kdeci(&w, "d", -5);
	ws_jw_kdeci(&w, "e", 120);
	ws_jw_key(&w, "arr");
	ws_jw_arr(&w);
	ws_jw_int(&w, 1);
	ws_jw_obj(&w);
	ws_jw_obj_end(&w);
	ws_jw_bool(&w, false);
	ws_jw_null(&w);
	ws_jw_arr_end(&w);
	ws_jw_obj_end(&w);
	zassert_true(w.ok);
	zassert_str_equal(
		buf, "{\"s\":\"a\\\"b\\u000a\",\"d\":-0.5,\"e\":12,\"arr\":[1,{},false,null]}");

	ws_jw_init(&w, buf, 8);
	ws_jw_str(&w, "too long for it");
	zassert_false(w.ok);
	zassert_equal(strlen(buf), 7);
}

ZTEST_SUITE(json, NULL, NULL, NULL, NULL, NULL);
