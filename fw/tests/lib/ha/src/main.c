/* MQTT payloads and Home Assistant discovery against samples. */
#include <string.h>
#include <zephyr/ztest.h>

#include <ws/config.h>
#include <ws/ha.h>

static struct ws_config cfg;
static struct ws_cfg_errors errs;
static struct ws_jtok toks[WS_JSON_TOKENS];
static const struct ws_ha_device dev = {"ws_a1b2", "1.2.3"};
static char topic[128], payload[2048];

static void *setup(void)
{
	zassert_ok(ws_cfg_compile((const char *)ws_factory_screens_json,
				  ws_factory_screens_json_len, &cfg, &errs, toks, WS_JSON_TOKENS));
	return NULL;
}

#define DEVICE                                                                                              \
	"\"device\":{\"identifiers\":[\"ws_a1b2\"],\"name\":\"Метеостанция\",\"manufacturer\":" \
	"\"radiolok\",\"model\":\"Mobitec 102x11 weatherstation\",\"sw_version\":\"1.2.3\"}"

ZTEST(ha, test_temperature_sensor)
{
	zassert_true(ws_ha_discovery(&dev, WS_HA_TEMP, &cfg, topic, sizeof(topic), payload,
				     sizeof(payload)) > 0);
	zassert_str_equal(topic, "homeassistant/sensor/ws_a1b2/t/config");
	zassert_str_equal(payload,
			  "{\"name\":\"Температура\",\"unique_id\":\"ws_a1b2_t\",\"object_id\":"
			  "\"ws_a1b2_t\",\"availability_topic\":\"ws/ws_a1b2/status\"," DEVICE
			  ",\"state_topic\":\"ws/ws_a1b2/sensors\",\"value_template\":\"{{ "
			  "value_json.t }}\",\"unit_of_measurement\":\"°C\",\"device_class\":"
			  "\"temperature\",\"state_class\":\"measurement\"}");
}

ZTEST(ha, test_lamp_light)
{
	ws_ha_discovery(&dev, WS_HA_LAMP, &cfg, topic, sizeof(topic), payload, sizeof(payload));
	zassert_str_equal(topic, "homeassistant/light/ws_a1b2/lamp/config");
	zassert_not_null(strstr(payload, "\"command_topic\":\"ws/ws_a1b2/lamp/set\""));
	zassert_not_null(strstr(payload, "\"state_topic\":\"ws/ws_a1b2/lamp/state\""));
	zassert_not_null(strstr(payload, "\"payload_on\":\"ON\",\"payload_off\":\"OFF\""));
}

ZTEST(ha, test_screen_select_lists_screens)
{
	ws_ha_discovery(&dev, WS_HA_SCREEN, &cfg, topic, sizeof(topic), payload, sizeof(payload));
	zassert_str_equal(topic, "homeassistant/select/ws_a1b2/screen/config");
	zassert_not_null(strstr(payload, "\"options\":[\"Авто\",\"Душно\",\"Дождь скоро\","
					 "\"Вечер дома\",\"Нет прогноза\",\"Обычный\"]"),
			 "%s", payload);
	zassert_not_null(strstr(payload, "\"command_topic\":\"ws/ws_a1b2/display/pin\""));
}

ZTEST(ha, test_all_entities_fit)
{
	for (int e = 0; e < WS_HA_ENTITY_COUNT; e++) {
		zassert_true(ws_ha_discovery(&dev, e, &cfg, topic, sizeof(topic), payload,
					     sizeof(payload)) > 0,
			     "entity %d", e);
		zassert_equal(strncmp(topic, "homeassistant/", 14), 0);
	}
	zassert_equal(ws_ha_discovery(&dev, WS_HA_ENTITY_COUNT, &cfg, topic, sizeof(topic), payload,
				      sizeof(payload)),
		      -1);
	/* too small buffer is reported */
	zassert_equal(ws_ha_discovery(&dev, WS_HA_TEMP, &cfg, topic, sizeof(topic), payload, 64),
		      -1);
}

ZTEST(ha, test_sensors_payload)
{
	struct ws_sensor_report r = {.t_ok = true,
				     .t = 224,
				     .rh_ok = true,
				     .rh = 410,
				     .p_ok = true,
				     .p = 7475,
				     .co2_ok = true,
				     .co2 = 640};

	ws_sensors_json(&r, payload, sizeof(payload));
	zassert_str_equal(payload, "{\"t\":22.4,\"rh\":41,\"p\":747.5,\"ptrend\":null,"
				   "\"co2\":640,\"rssi\":null}");
	ws_display_state_json("rain", "Дождь скоро", "fc.rain_in=120 <= 180", false, 42, payload,
			      sizeof(payload));
	zassert_str_equal(payload, "{\"screen\":\"rain\",\"name\":\"Дождь скоро\",\"reason\":"
				   "\"fc.rain_in=120 <= 180\",\"pinned\":false,\"flips_24h\":42}");
}

ZTEST(ha, test_commands)
{
	int s;
	uint32_t m;

	zassert_equal(ws_parse_onoff("ON", 2), 1);
	zassert_equal(ws_parse_onoff("off\n", 4), 0);
	zassert_equal(ws_parse_onoff("maybe", 5), -1);

	zassert_ok(ws_parse_pin(&cfg, "auto", 4, &s, &m));
	zassert_equal(s, -1);
	zassert_ok(ws_parse_pin(&cfg, "Авто", strlen("Авто"), &s, &m));
	zassert_equal(s, -1);
	const char *j = "{\"screen\": \"rain\", \"minutes\": 30}";

	zassert_ok(ws_parse_pin(&cfg, j, strlen(j), &s, &m));
	zassert_equal(s, ws_cfg_screen_by_id(&cfg, "rain"));
	zassert_equal(m, 30);
	zassert_ok(ws_parse_pin(&cfg, "Вечер дома", strlen("Вечер дома"), &s, &m));
	zassert_equal(s, ws_cfg_screen_by_id(&cfg, "evening"));
	zassert_ok(ws_parse_pin(&cfg, "{\"screen\":null}", 15, &s, &m));
	zassert_equal(s, -1);
	zassert_not_equal(ws_parse_pin(&cfg, "nope", 4, &s, &m), 0);
	zassert_not_equal(ws_parse_pin(&cfg, "{\"screen\":\"main\",\"minutes\":-1}", 31, &s, &m),
			  0);
}

ZTEST_SUITE(ha, NULL, setup, NULL, NULL, NULL);
