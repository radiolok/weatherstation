/* OTA state machine: request checks, download, verification, self-test. */
#include <string.h>
#include <zephyr/ztest.h>

#include <ws/ota.h>
#include <ws/util.h>

#define SHA "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"

static struct ws_ota o;

static void req(const char *json, enum ws_ota_action want)
{
	zassert_equal(ws_ota_request(&o, json, strlen(json), 100), want, "%s: %s", json, o.error);
}

static void before(void *f)
{
	ws_ota_init(&o);
}

ZTEST(ota, test_request_checks)
{
	req("{\"url\":\"http://10.0.0.2/ws.bin\",\"sha256\":\"" SHA "\",\"version\":\"1.2.0\"}",
	    WS_OTA_ACT_DOWNLOAD);
	zassert_equal(o.state, WS_OTA_DOWNLOAD);
	zassert_equal(o.source, WS_OTA_SRC_MQTT);
	zassert_equal(o.sha256[0], 0x9f);
	zassert_str_equal(o.version, "1.2.0");
	/* a second request while busy is refused and does not disturb the first */
	req("{\"url\":\"http://x/y\",\"sha256\":\"" SHA "\"}", WS_OTA_ACT_NONE);
	zassert_equal(o.state, WS_OTA_DOWNLOAD);
	zassert_true(strstr(o.error, "занято") != NULL);

	ws_ota_init(&o);
	req("{\"url\":\"ftp://x/y\",\"sha256\":\"" SHA "\"}", WS_OTA_ACT_NONE);
	zassert_equal(o.state, WS_OTA_FAILED);
	req("{\"url\":\"http://x/y\",\"sha256\":\"abc\"}", WS_OTA_ACT_NONE);
	zassert_true(strstr(o.error, "sha256") != NULL);
	req("{\"url\":\"http://x/"
	    "y\",\"sha256\":\"zz86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\"}",
	    WS_OTA_ACT_NONE);
	req("not json", WS_OTA_ACT_NONE);
	/* after a failure a new request is accepted */
	req("{\"url\":\"https://x/y\",\"sha256\":\"" SHA "\"}", WS_OTA_ACT_DOWNLOAD);
}

ZTEST(ota, test_happy_path)
{
	uint8_t sha[32];

	ws_unhex(SHA, sha, 32);
	req("{\"url\":\"http://h/ws.bin\",\"sha256\":\"" SHA "\"}", WS_OTA_ACT_DOWNLOAD);
	zassert_equal(ws_ota_progress(&o, 1000), WS_OTA_ACT_NONE);
	zassert_equal(ws_ota_percent(&o), -1);
	zassert_equal(ws_ota_download_done(&o, 0, 110), WS_OTA_ACT_VERIFY);
	zassert_equal(o.size, 1000);
	zassert_equal(ws_ota_verified(&o, sha, 111), WS_OTA_ACT_MARK);
	zassert_equal(o.state, WS_OTA_READY);
	zassert_equal(ws_ota_marked(&o, 0, 111), WS_OTA_ACT_NONE);
	zassert_equal(ws_ota_tick(&o, 112), WS_OTA_ACT_NONE, "the page gets time to show 'ready'");
	zassert_equal(ws_ota_tick(&o, 111 + WS_OTA_REBOOT_DELAY), WS_OTA_ACT_REBOOT);
}

ZTEST(ota, test_sha_mismatch_and_errors)
{
	uint8_t other[32] = {1};

	req("{\"url\":\"http://h/ws.bin\",\"sha256\":\"" SHA "\"}", WS_OTA_ACT_DOWNLOAD);
	ws_ota_progress(&o, 10);
	ws_ota_download_done(&o, 0, 101);
	zassert_equal(ws_ota_verified(&o, other, 102), WS_OTA_ACT_NONE);
	zassert_equal(o.state, WS_OTA_FAILED);
	zassert_str_equal(o.error, "sha256 не совпадает");

	req("{\"url\":\"http://h/ws.bin\",\"sha256\":\"" SHA "\"}", WS_OTA_ACT_DOWNLOAD);
	zassert_equal(ws_ota_download_done(&o, -110, 105), WS_OTA_ACT_NONE);
	zassert_equal(o.state, WS_OTA_FAILED);
	zassert_true(strstr(o.error, "-110") != NULL);

	req("{\"url\":\"http://h/ws.bin\",\"sha256\":\"" SHA "\"}", WS_OTA_ACT_DOWNLOAD);
	zassert_equal(ws_ota_download_done(&o, 0, 105), WS_OTA_ACT_NONE, "empty body");
	zassert_equal(o.state, WS_OTA_FAILED);

	/* marking fails: no reboot */
	uint8_t sha[32];

	ws_unhex(SHA, sha, 32);
	req("{\"url\":\"http://h/ws.bin\",\"sha256\":\"" SHA "\"}", WS_OTA_ACT_DOWNLOAD);
	ws_ota_progress(&o, 10);
	ws_ota_download_done(&o, 0, 101);
	ws_ota_verified(&o, sha, 101);
	ws_ota_marked(&o, -5, 101);
	zassert_equal(o.state, WS_OTA_FAILED);
	zassert_equal(ws_ota_tick(&o, 1000), WS_OTA_ACT_NONE);
}

ZTEST(ota, test_upload_by_size)
{
	uint8_t sha[32];

	ws_unhex(SHA, sha, 32);
	zassert_equal(ws_ota_upload_begin(&o, WS_OTA_SRC_WEB, 3000, sha, 50), WS_OTA_ACT_NONE);
	zassert_equal(o.state, WS_OTA_DOWNLOAD);
	zassert_equal(ws_ota_progress(&o, 1500), WS_OTA_ACT_NONE);
	zassert_equal(ws_ota_percent(&o), 50);
	zassert_equal(ws_ota_progress(&o, 1500), WS_OTA_ACT_VERIFY);
	zassert_equal(ws_ota_download_done(&o, 0, 51), WS_OTA_ACT_NONE, "already verifying");
	zassert_equal(ws_ota_verified(&o, sha, 52), WS_OTA_ACT_MARK);

	ws_ota_init(&o);
	ws_ota_upload_begin(&o, WS_OTA_SRC_WEB, 100, sha, 50);
	ws_ota_progress(&o, 101);
	zassert_equal(o.state, WS_OTA_FAILED, "longer than announced");

	ws_ota_init(&o);
	ws_ota_upload_begin(&o, WS_OTA_SRC_WEB, 100, sha, 50);
	ws_ota_progress(&o, 99);
	zassert_equal(ws_ota_download_done(&o, 0, 60), WS_OTA_ACT_NONE);
	zassert_equal(o.state, WS_OTA_FAILED, "shorter than announced");

	ws_ota_init(&o);
	ws_ota_upload_begin(&o, WS_OTA_SRC_SMP, 10, NULL, 50);
	ws_ota_progress(&o, 10);
	zassert_equal(ws_ota_verified(&o, sha, 51), WS_OTA_ACT_MARK, "SMP: no hash to compare");
}

ZTEST(ota, test_selftest_confirm)
{
	zassert_equal(ws_ota_boot(&o, false, 5), WS_OTA_ACT_NONE);
	zassert_equal(o.state, WS_OTA_TESTING);
	zassert_false(o.confirmed);
	zassert_equal(ws_ota_tick(&o, 60), WS_OTA_ACT_NONE);
	/* an update request during the self-test is refused */
	req("{\"url\":\"http://h/ws.bin\",\"sha256\":\"" SHA "\"}", WS_OTA_ACT_NONE);
	zassert_equal(ws_ota_mqtt_connected(&o, 70), WS_OTA_ACT_CONFIRM);
	zassert_true(o.confirmed);
	zassert_equal(o.state, WS_OTA_IDLE);
	zassert_equal(ws_ota_tick(&o, 500), WS_OTA_ACT_NONE);
	zassert_equal(ws_ota_mqtt_connected(&o, 501), WS_OTA_ACT_NONE, "only once");
}

ZTEST(ota, test_selftest_timeout_reverts)
{
	ws_ota_boot(&o, false, 5);
	zassert_equal(ws_ota_tick(&o, 5 + WS_OTA_SELFTEST_S - 1), WS_OTA_ACT_NONE);
	zassert_equal(ws_ota_tick(&o, 5 + WS_OTA_SELFTEST_S), WS_OTA_ACT_REBOOT);
	zassert_str_equal(o.error, "самопроверка не пройдена");
	zassert_equal(ws_ota_tick(&o, 5 + WS_OTA_SELFTEST_S + 1), WS_OTA_ACT_NONE, "once");
}

ZTEST(ota, test_confirmed_boot)
{
	zassert_equal(ws_ota_boot(&o, true, 5), WS_OTA_ACT_NONE);
	zassert_equal(o.state, WS_OTA_IDLE);
	zassert_equal(ws_ota_mqtt_connected(&o, 6), WS_OTA_ACT_NONE);
	zassert_str_equal(ws_ota_state_name(WS_OTA_TESTING), "testing");
}

ZTEST(ota, test_zero_hash_is_still_checked)
{
	/* an all-zero sha256 in a request is a value to compare, not "none" */
	uint8_t sha[32];

	ws_unhex(SHA, sha, 32);
	req("{\"url\":\"http://h/ws.bin\",\"sha256\":\""
	    "0000000000000000000000000000000000000000000000000000000000000000\"}",
	    WS_OTA_ACT_DOWNLOAD);
	ws_ota_progress(&o, 10);
	ws_ota_download_done(&o, 0, 101);
	zassert_equal(ws_ota_verified(&o, sha, 102), WS_OTA_ACT_NONE);
	zassert_equal(o.state, WS_OTA_FAILED);
}

ZTEST_SUITE(ota, NULL, NULL, before, NULL, NULL);
