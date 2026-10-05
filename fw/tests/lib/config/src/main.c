/* Screen configuration: validation with error paths, factory set, round trip. */
#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>

#include <ws/config.h>
#include <ws/vars.h>

static struct ws_config cfg, cfg2;
static struct ws_cfg_errors errs;
static struct ws_jtok toks[WS_JSON_TOKENS];
static char buf[WS_MAX_FILE];

static int compile(const char *json)
{
	return ws_cfg_compile(json, strlen(json), &cfg, &errs, toks, WS_JSON_TOKENS);
}

static void dump_errors(void)
{
	for (int i = 0; i < errs.count && i < WS_MAX_ERRORS; i++) {
		TC_PRINT("  %s: %s\n", errs.e[i].path, errs.e[i].msg);
	}
}

/* Expects a failure with an error at `path` whose message contains `msg`. */
static void expect_error(const char *json, const char *path, const char *msg)
{
	zassert_equal(compile(json), -1, "accepted: %s", json);
	for (int i = 0; i < errs.count && i < WS_MAX_ERRORS; i++) {
		if (strcmp(errs.e[i].path, path) == 0 && strstr(errs.e[i].msg, msg)) {
			return;
		}
	}
	dump_errors();
	zassert_unreachable("no error \"%s: %s\"", path, msg);
}

#define MAIN         "{\"id\":\"main\",\"default\":true,\"items\":[]}"
#define CFG(screens) "{\"schema\":1,\"screens\":[" screens "]}"

ZTEST(config, test_factory_set_compiles)
{
	int r = ws_cfg_compile((const char *)ws_factory_screens_json, ws_factory_screens_json_len,
			       &cfg, &errs, toks, WS_JSON_TOKENS);

	if (r) {
		dump_errors();
	}
	zassert_ok(r);
	zassert_equal(cfg.n_screens, 5);
	zassert_str_equal(cfg.screens[cfg.def].id, "main");
	int s = ws_cfg_screen_by_id(&cfg, "stuffy");

	zassert_true(s >= 0);
	zassert_equal(cfg.screens[s].rule.prio, 70);
	zassert_equal(cfg.screens[s].rule.on_delay, 120);
	zassert_equal(cfg.screens[s].rule.min_show, 60);
	const struct ws_cmp *c = &cfg.cmps[cfg.screens[s].rule.cond.first];

	zassert_equal(c->var, WS_V_IN_CO2);
	zassert_equal(c->v[0], 10000);
	zassert_equal(c->hyst, 1000);
	/* evening: time window through midnight, no condition */
	s = ws_cfg_screen_by_id(&cfg, "evening");
	zassert_equal(cfg.screens[s].rule.from_min, 22 * 60);
	zassert_equal(cfg.screens[s].rule.to_min, 7 * 60);
	zassert_equal(cfg.screens[s].rule.cond.n, 0);
	/* main: the icon has the skid alternative */
	const struct ws_screen *m = &cfg.screens[cfg.def];
	const struct ws_item *icon = &cfg.items[m->items + 1];

	zassert_equal(icon->type, WS_IT_ICON);
	zassert_equal(icon->n_alts, 1);
	zassert_equal(cfg.items[icon->alts].type, WS_IT_PICTO);
	zassert_equal(cfg.items[icon->alts].x, 33);
	TC_PRINT("compiled config: %u bytes, %u items, %u comparisons, %u string bytes\n",
		 (unsigned int)sizeof(cfg), cfg.n_items, cfg.n_cmps, cfg.n_str);
}

ZTEST(config, test_round_trip)
{
	zassert_ok(ws_cfg_compile((const char *)ws_factory_screens_json,
				  ws_factory_screens_json_len, &cfg, &errs, toks, WS_JSON_TOKENS));
	int n = ws_cfg_to_json(&cfg, buf, sizeof(buf));

	zassert_true(n > 0);
	int r = ws_cfg_compile(buf, n, &cfg2, &errs, toks, WS_JSON_TOKENS);

	if (r) {
		dump_errors();
		TC_PRINT("%s\n", buf);
	}
	zassert_ok(r);
	zassert_mem_equal(&cfg, &cfg2, sizeof(cfg));
	/* and the canonical text is stable */
	static char again[WS_MAX_FILE];

	zassert_equal(ws_cfg_to_json(&cfg2, again, sizeof(again)), n);
	zassert_str_equal(buf, again);
}

ZTEST(config, test_round_trip_full_features)
{
	const char *json =
		"{\"schema\":1,\"screens\":[" MAIN ",{\"id\":\"x\",\"name\":\"X\","
		"\"rule\":{\"any\":[{\"var\":\"out.cond\",\"op\":\"in\",\"val\":[\"rain\","
		"\"snow\"]},{\"all\":[{\"var\":\"in.t\",\"op\":\"between\",\"val\":[18,"
		"24.5],\"hyst\":0.5},{\"var\":\"sun.up\",\"op\":\"=\",\"val\":false}]}],"
		"\"time\":{\"from\":\"08:30\",\"to\":\"20:00\",\"days\":[1,2,3,4,5]},"
		"\"prio\":60,\"mode\":\"insert\",\"insert\":{\"show\":20,\"every\":5}},"
		"\"items\":[{\"type\":\"number\",\"var\":\"ext.1\",\"form\":\"S\",\"y\":6,"
		"\"prefix\":\"T\",\"suffix\":\"%\",\"decimals\":1,\"picto\":\"mine\"},"
		"{\"type\":\"sep\",\"x\":50,\"dashed\":true},{\"type\":\"text\",\"x\":60,"
		"\"text\":\"ДОМА\"},{\"type\":\"clock\",\"form\":\"S\",\"x\":60,\"y\":6}]}],"
		"\"ext\":[{\"id\":\"ext.1\",\"name\":\"Балкон\",\"topic\":\"z2m/balcony\","
		"\"field\":\"temperature\",\"unit\":\"°C\",\"ttl\":600}],"
		"\"pictos\":[{\"name\":\"mine\",\"size\":5,\"rows\":[\"1f\",\"11\",\"11\","
		"\"11\",\"1f\"]}]}";
	int r = compile(json);

	if (r) {
		dump_errors();
	}
	zassert_ok(r);
	int n = ws_cfg_to_json(&cfg, buf, sizeof(buf));

	zassert_true(n > 0);
	zassert_ok(ws_cfg_compile(buf, n, &cfg2, &errs, toks, WS_JSON_TOKENS));
	zassert_mem_equal(&cfg, &cfg2, sizeof(cfg));
	zassert_equal(cfg.n_ext, 1);
	zassert_equal(cfg.ext[0].ttl, 600);
	zassert_not_null(ws_cfg_picto(&cfg, "mine", false));
	zassert_is_null(ws_cfg_picto(&cfg, "mine", true));
	zassert_equal(cfg.screens[1].rule.days, 0x1F);
}

ZTEST(config, test_truncated_json)
{
	const char *full = (const char *)ws_factory_screens_json;

	for (size_t cut = 0; cut < ws_factory_screens_json_len - 3; cut += 97) {
		zassert_equal(ws_cfg_compile(full, cut, &cfg, &errs, toks, WS_JSON_TOKENS), -1);
		zassert_true(errs.count >= 1);
	}
	zassert_equal(compile("{\"schema\":1,\"screens\":["), -1);
	zassert_not_null(strstr(errs.e[0].msg, "обрезан"));
}

ZTEST(config, test_overlap_path)
{
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":["
			 "{\"type\":\"icon\",\"x\":0},{\"type\":\"icon\",\"x\":20},"
			 "{\"type\":\"icon\",\"x\":40},{\"type\":\"temp\",\"x\":45}]}"),
		     "screens[0].items[3]", "перекрывает items[2]");
	expect_error(CFG(MAIN
			 ",{\"id\":\"b\",\"rule\":{\"time\":{\"from\":\"01:00\","
			 "\"to\":\"02:00\"}},\"items\":[{\"type\":\"picto\",\"name\":\"home\"},"
			 "{\"type\":\"picto\",\"name\":\"home\",\"x\":5}]}"),
		     "screens[1].items[1]", "перекрывает items[0]");
}

ZTEST(config, test_unknown_variable)
{
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"temp\","
			 "\"var\":\"out.temp\"}]}"),
		     "screens[0].items[0]", "неизвестная переменная «out.temp»");
	expect_error(CFG(MAIN ",{\"id\":\"b\",\"rule\":{\"all\":[{\"var\":\"in.co3\",\"op\":\">\","
			      "\"val\":1}]},\"items\":[]}"),
		     "screens[1].rule.all[0]", "неизвестная переменная");
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"icon\","
			 "\"var\":\"in.t\"}]}"),
		     "screens[0].items[0]", "не подходит");
}

ZTEST(config, test_defaults_count)
{
	expect_error(CFG(MAIN "," MAIN), "screens", "ровно один, а их 2");
	expect_error(CFG("{\"id\":\"a\",\"items\":[]}"), "screens", "ровно один, а их 0");
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"rule\":{\"time\":{\"from\":\"01:00\","
			 "\"to\":\"02:00\"}},\"items\":[]}"),
		     "screens[0]", "не бывает правила");
}

ZTEST(config, test_limits)
{
	/* 17 screens */
	char *p = buf;

	p += sprintf(p, "{\"schema\":1,\"screens\":[" MAIN);
	for (int i = 0; i < 16; i++) {
		p += sprintf(p,
			     ",{\"id\":\"s%d\",\"rule\":{\"time\":{\"from\":\"01:00\","
			     "\"to\":\"02:00\"}},\"items\":[]}",
			     i);
	}
	sprintf(p, "]}");
	expect_error(buf, "screens", "экранов больше 16");

	/* 13 items */
	p = buf + sprintf(buf, "%s",
			  "{\"schema\":1,\"screens\":[{\"id\":\"a\",\"default\":true,\"items\":[");
	for (int i = 0; i < 13; i++) {
		p += sprintf(p, "%s{\"type\":\"sep\",\"x\":%d}", i ? "," : "", i * 2);
	}
	sprintf(p, "]}]}");
	expect_error(buf, "screens[0]", "элементов больше 12");

	/* 5 rotators */
	p = buf + sprintf(buf, "%s",
			  "{\"schema\":1,\"screens\":[{\"id\":\"a\",\"default\":true,\"items\":[");
	for (int i = 0; i < 5; i++) {
		p += sprintf(p,
			     "%s{\"type\":\"rotator\",\"x\":%d,\"w\":12,\"items\":[{\"type\":"
			     "\"sep\"}]}",
			     i ? "," : "", i * 14);
	}
	sprintf(p, "]}]}");
	expect_error(buf, "screens[0]", "зон ротации больше 4");

	/* 9 comparisons in a rule */
	p = buf;
	p += sprintf(p, "{\"schema\":1,\"screens\":[" MAIN ",{\"id\":\"b\",\"rule\":{\"all\":[");
	for (int i = 0; i < 9; i++) {
		p += sprintf(p, "%s{\"var\":\"in.t\",\"op\":\">\",\"val\":%d}", i ? "," : "", i);
	}
	sprintf(p, "]},\"items\":[]}]}");
	expect_error(buf, "screens[1].rule", "сравнений больше 8");

	/* rotator with 9 items */
	p = buf +
	    sprintf(buf, "{\"schema\":1,\"screens\":[{\"id\":\"a\",\"default\":true,\"items\":[");
	p += sprintf(p, "{\"type\":\"rotator\",\"w\":20,\"items\":[");
	for (int i = 0; i < 9; i++) {
		p += sprintf(p, "%s{\"type\":\"sep\"}", i ? "," : "");
	}
	sprintf(p, "]}]}]}");
	expect_error(buf, "screens[0].items[0]", "больше 8 элементов");

	/* file size */
	memset(buf, ' ', sizeof(buf));
	zassert_equal(ws_cfg_compile(buf, sizeof(buf) + 1, &cfg, &errs, toks, WS_JSON_TOKENS), -1);
}

ZTEST(config, test_placement)
{
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"temp\",\"y\":3}]}"),
		     "screens[0].items[0]", "y = 0");
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"humidity\","
			 "\"y\":2}]}"),
		     "screens[0].items[0]", "y = 0, 3 или 6");
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"temp\","
			 "\"x\":90}]}"),
		     "screens[0].items[0]", "за холст");
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"rotator\","
			 "\"x\":0,\"w\":10,\"items\":[{\"type\":\"icon\",\"x\":5,\"w\":11}]}]}"),
		     "screens[0].items[0].items[0]", "за рамку зоны");
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"icon\","
			 "\"w\":8}]}"),
		     "screens[0].items[0]", "уже");
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"wind\","
			 "\"form\":\"L\"}]}"),
		     "screens[0].items[0]", "нет формы «L»");
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"flower\"}]}"),
		     "screens[0].items[0]", "неизвестный тип");
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"icon\","
			 "\"colour\":1}]}"),
		     "screens[0].items[0]", "неизвестное поле «colour»");
	expect_error(CFG("{\"id\":\"a b\",\"default\":true,\"items\":[]}"), "screens[0]", "«id»");
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"picto\","
			 "\"name\":\"unicorn\"}]}"),
		     "screens[0].items[0]", "нет пиктограммы «unicorn»");
}

ZTEST(config, test_alternatives)
{
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"icon\","
			 "\"alts\":[{\"type\":\"picto\",\"name\":\"skid\"}]}]}"),
		     "screens[0].items[0].alts[0]", "условие «when»");
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"icon\",\"form\":"
			 "\"S\",\"alts\":[{\"type\":\"picto\",\"name\":\"skid\",\"when\":{\"all\":"
			 "[{\"var\":\"fc.ice\",\"op\":\"=\",\"val\":true}]}}]}]}"),
		     "screens[0].items[0].alts[0]", "выше рамки");
	expect_error(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"icon\",\"when\":"
			 "{\"all\":[{\"var\":\"fc.ice\",\"op\":\"=\",\"val\":\"yes\"}]}}]}"),
		     "screens[0].items[0].when.all[0]", "true или false");
	expect_error(
		CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"icon\",\"when\":"
		    "{\"all\":[{\"any\":[{\"var\":\"fc.ice\",\"op\":\"=\",\"val\":true}]}]}}]}"),
		"screens[0].items[0].when.all[0]", "вложенности");
}

ZTEST(config, test_rule_fields)
{
	expect_error(CFG(MAIN ",{\"id\":\"b\",\"rule\":{\"prio\":50},\"items\":[]}"),
		     "screens[1].rule", "ни условия, ни окна");
	expect_error(CFG(MAIN ",{\"id\":\"b\",\"rule\":{\"time\":{\"from\":\"25:00\","
			      "\"to\":\"02:00\"}},\"items\":[]}"),
		     "screens[1].rule.time", "ЧЧ:ММ");
	expect_error(CFG(MAIN ",{\"id\":\"b\",\"rule\":{\"time\":{\"from\":\"01:00\","
			      "\"to\":\"02:00\"},\"prio\":100},\"items\":[]}"),
		     "screens[1].rule", "«prio» = 100");
	expect_error(CFG(MAIN ",{\"id\":\"b\",\"rule\":{\"all\":[{\"var\":\"in.t\",\"op\":\"~\","
			      "\"val\":1}]},\"items\":[]}"),
		     "screens[1].rule.all[0]", "неизвестная операция");
	expect_error(CFG(MAIN ",{\"id\":\"main\",\"rule\":{\"time\":{\"from\":\"01:00\","
			      "\"to\":\"02:00\"}},\"items\":[]}"),
		     "screens[1]", "уже есть");
	expect_error(CFG(MAIN ",{\"id\":\"b\",\"rule\":{\"time\":{\"from\":\"01:00\","
			      "\"to\":\"02:00\"},\"mode\":\"insert\"},\"items\":[]}"),
		     "screens[1].rule", "«insert»");
}

ZTEST(config, test_pictos_and_ext)
{
	expect_error("{\"schema\":1,\"screens\":[" MAIN "],\"pictos\":[{\"name\":\"a\",\"size\":"
		     "5,\"rows\":[\"20\",\"0\",\"0\",\"0\",\"0\"]}]}",
		     "pictos[0]", "не помещается");
	expect_error("{\"schema\":1,\"screens\":[" MAIN "],\"pictos\":[{\"name\":\"A\",\"size\":"
		     "5,\"rows\":[\"1\",\"0\",\"0\",\"0\",\"0\"]}]}",
		     "pictos[0]", "«name»");
	expect_error("{\"schema\":1,\"screens\":[" MAIN "],\"ext\":[{\"id\":\"ext.9\","
		     "\"topic\":\"a\"}]}",
		     "ext[0]", "ext.1");
	expect_error("{\"schema\":1,\"screens\":[" MAIN "],\"ext\":[{\"id\":\"ext.1\"}]}", "ext[0]",
		     "топик");
	expect_error("{\"schema\":2,\"screens\":[" MAIN "]}", "schema", "схема 1");
}

ZTEST(config, test_errors_json)
{
	char out[512];
	struct ws_jw w;

	compile(CFG("{\"id\":\"a\",\"default\":true,\"items\":[{\"type\":\"icon\",\"x\":0},"
		    "{\"type\":\"icon\",\"x\":5}]}"));
	ws_jw_init(&w, out, sizeof(out));
	ws_cfg_errors_json(&errs, &w);
	zassert_true(w.ok);
	zassert_str_equal(out,
			  "[{\"path\":\"screens[0].items[1]\",\"msg\":\"перекрывает items[0]\"}]");
}

ZTEST(config, test_catalog)
{
	struct ws_json j;
	int n = ws_catalog_json(NULL, buf, sizeof(buf));

	zassert_true(n > 0);
	zassert_true(ws_json_parse(&j, buf, n, toks, WS_JSON_TOKENS) > 0);
	int types = ws_json_get(&j, 0, "types");

	zassert_equal(j.t[types].size, WS_IT_COUNT);
	zassert_not_null(strstr(buf, "\"type\":\"wind\",\"label\":\"Ветер\""));
	zassert_not_null(strstr(buf, "\"vars\":[\"out.wind\",\"obs.wind\"]"));
	zassert_not_null(strstr(buf, "\"skid\""));
}

static int read_broken(void *ctx, char *b, size_t cap)
{
	const char *s = "{\"schema\":1,\"screens\":[{\"id\"";

	strcpy(b, s);
	return (int)strlen(s);
}

static int read_missing(void *ctx, char *b, size_t cap)
{
	return -2; /* -ENOENT */
}

static int read_factory(void *ctx, char *b, size_t cap)
{
	memcpy(b, ws_factory_screens_json, ws_factory_screens_json_len);
	return (int)ws_factory_screens_json_len;
}

static int read_text(void *ctx, char *b, size_t cap)
{
	strcpy(b, ctx);
	return (int)strlen(b);
}

ZTEST(config, test_load_chain)
{
	const struct ws_cfg_source broken_then_factory[] = {
		{"screens.json", read_broken, NULL},
		{"screens.prev.json", read_missing, NULL},
		{"factory", read_factory, NULL},
	};

	zassert_equal(ws_cfg_load_chain(broken_then_factory, 3, buf, sizeof(buf), &cfg, &errs, toks,
					WS_JSON_TOKENS),
		      2);
	zassert_equal(cfg.n_screens, 5);

	const struct ws_cfg_source prev_ok[] = {
		{"screens.json", read_broken, NULL},
		{"screens.prev.json", read_text, (void *)CFG(MAIN)},
		{"factory", read_factory, NULL},
	};

	zassert_equal(
		ws_cfg_load_chain(prev_ok, 3, buf, sizeof(buf), &cfg, &errs, toks, WS_JSON_TOKENS),
		1);
	zassert_equal(cfg.n_screens, 1);
	zassert_equal(ws_cfg_load_chain(broken_then_factory, 2, buf, sizeof(buf), &cfg, &errs, toks,
					WS_JSON_TOKENS),
		      -1);
}

ZTEST_SUITE(config, NULL, NULL, NULL, NULL, NULL);
