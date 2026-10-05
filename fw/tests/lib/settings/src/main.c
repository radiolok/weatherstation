/* Device settings: JSON, validation, secrets, password hash. */
#include <string.h>
#include <zephyr/ztest.h>

#include <ws/settings.h>

static struct ws_settings s;
static struct ws_cfg_errors errs;
static char buf[4096];

static void before(void *f)
{
	ws_settings_defaults(&s);
}

static int apply(const char *json, uint64_t *ch)
{
	return ws_settings_apply_json(&s, json, strlen(json), "salt1234", ch, &errs);
}

ZTEST(settings, test_defaults_json_hides_secrets)
{
	strcpy(s.wifi_psk, "secret");
	zassert_true(ws_settings_to_json(&s, buf, sizeof(buf)) > 0);
	zassert_is_null(strstr(buf, "secret"));
	zassert_not_null(strstr(buf, "\"wifi.psk_set\":true"));
	zassert_not_null(strstr(buf, "\"mqtt.port\":1883"));
	zassert_not_null(strstr(buf, "\"ntp.tz\":\"MSK-3\""));
	zassert_not_null(strstr(buf, "\"lamp.restore\":\"last\""));
	zassert_is_null(strstr(buf, "web.salt"));
}

ZTEST(settings, test_apply_partial)
{
	uint64_t ch;

	zassert_ok(apply("{\"wifi.ssid\":\"home\",\"wifi.psk\":\"pw\",\"mqtt.port\":1884,"
			 "\"lamp.restore\":\"on\",\"metar.icao\":\"UWGG\"}",
			 &ch));
	zassert_str_equal(s.wifi_ssid, "home");
	zassert_equal(s.mqtt_port, 1884);
	zassert_equal(s.lamp_restore, WS_LAMP_RESTORE_ON);
	zassert_true(ch & (1ULL << 0));
	zassert_true(ch & (1ULL << 3));
	char url[160];

	ws_settings_metar_url(&s, 0, url, sizeof(url));
	zassert_str_equal(url,
			  "https://tgftp.nws.noaa.gov/data/observations/metar/stations/UWGG.TXT");
	ws_settings_metar_url(&s, 1, url, sizeof(url));
	zassert_str_equal(url, "https://aviationweather.gov/api/data/metar?ids=UWGG&format=raw");
}

ZTEST(settings, test_invalid_changes_nothing)
{
	uint64_t ch;

	zassert_equal(apply("{\"wifi.ssid\":\"x\",\"mqtt.port\":70000}", &ch), -1);
	zassert_str_equal(s.wifi_ssid, "");
	zassert_str_equal(errs.e[0].path, "mqtt.port");
	zassert_equal(apply("{\"ntp.tz\":\"Moscow\"}", &ch), -1);
	zassert_str_equal(errs.e[0].path, "ntp.tz");
	zassert_equal(apply("{\"metar.icao\":\"uwgg\"}", &ch), -1);
	zassert_equal(apply("{\"metar.url1\":\"http://x\"}", &ch), -1);
	zassert_equal(apply("{\"web.hash\":\"00\"}", &ch), -1);
	zassert_equal(apply("{\"nope\":1}", &ch), -1);
	zassert_equal(apply("[1]", &ch), -1);
}

ZTEST(settings, test_password)
{
	uint64_t ch;

	zassert_false(ws_settings_check_password(&s, "admin"));
	zassert_ok(apply("{\"web.password\":\"correct horse\"}", &ch));
	zassert_true(ws_settings_check_password(&s, "correct horse"));
	zassert_false(ws_settings_check_password(&s, "correct horsE"));
	zassert_str_equal(s.web_salt, "salt1234");
	zassert_equal(strlen(s.web_hash), 64);
	zassert_equal(apply("{\"web.password\":\"abc\"}", &ch), -1);
}

ZTEST_SUITE(settings, NULL, NULL, before, NULL, NULL);
