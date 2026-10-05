/* Pressure trend, MH-Z19B frames, BME280 math. */
#include <zephyr/ztest.h>

#include <ws/sensors.h>

ZTEST(sensors, test_trend_needs_3h)
{
	struct ws_ptrend t;
	int32_t d;

	ws_ptrend_init(&t);
	/* falling 1 mm per hour, sample every 30 s */
	for (int64_t s = 0; s < 3 * 3600; s += 30) {
		ws_ptrend_add(&t, 7500 - (int32_t)(s * 10 / 3600), s);
	}
	zassert_false(ws_ptrend_get(&t, &d));
	for (int64_t s = 3 * 3600; s < 3 * 3600 + 600; s += 30) {
		ws_ptrend_add(&t, 7500 - (int32_t)(s * 10 / 3600), s);
	}
	zassert_true(ws_ptrend_get(&t, &d));
	zassert_within(d, -30, 1, "%d", d);
}

ZTEST(sensors, test_trend_gap)
{
	struct ws_ptrend t;
	int32_t d;

	ws_ptrend_init(&t);
	ws_ptrend_add(&t, 7400, 0);
	/* the sensor was silent for 4 h: the old average fills the gap */
	ws_ptrend_add(&t, 7450, 4 * 3600);
	ws_ptrend_add(&t, 7450, 4 * 3600 + 300);
	zassert_true(ws_ptrend_get(&t, &d));
	zassert_equal(d, 50);
}

ZTEST(sensors, test_mmhg)
{
	zassert_equal(ws_pa_to_mmhg_deci(101325), 7600);
	zassert_equal(ws_pa_to_mmhg_deci(99725), 7480);
}

ZTEST(sensors, test_mhz19b)
{
	/* command "read CO2" from the datasheet */
	const uint8_t cmd[9] = {0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0x79};
	uint8_t r[9];
	uint16_t ppm;

	zassert_equal(ws_mhz19b_checksum(cmd), 0x79);
	ws_mhz19b_response(r, 0x86, 1240);
	zassert_ok(ws_mhz19b_parse(r, &ppm));
	zassert_equal(ppm, 1240);
	r[3] ^= 1;
	zassert_not_equal(ws_mhz19b_parse(r, &ppm), 0);
}

ZTEST(sensors, test_bme280_datasheet_example)
{
	/* Bosch datasheet example: adc_T 519888 -> 25.08 degC,
	 * adc_P 415148 -> 100653 Pa */
	const struct ws_bme280_calib *c = &ws_bme280_calib_example;
	int32_t tf;

	zassert_equal(ws_bme280_temp(c, 519888, &tf), 2508);
	zassert_equal(ws_bme280_press(c, 415148, tf) >> 8, 100653);
}

ZTEST(sensors, test_bme280_inverse)
{
	const struct ws_bme280_calib *c = &ws_bme280_calib_example;
	const int32_t temps[] = {-3550, -1000, 0, 2215, 4000};

	for (unsigned int i = 0; i < sizeof(temps) / sizeof(temps[0]); i++) {
		int32_t at, ap, ah, tf;

		ws_bme280_raw_for(c, temps[i], 99725, 41 * 1024, &at, &ap, &ah);
		zassert_within(ws_bme280_temp(c, at, &tf), temps[i], 1);
		zassert_within((int32_t)(ws_bme280_press(c, ap, tf) >> 8), 99725, 2);
		zassert_within((int32_t)ws_bme280_hum(c, ah, tf), 41 * 1024, 64);
	}
	uint8_t r88[26], re1[7];

	ws_bme280_pack_calib(c, r88, re1);
	zassert_equal(r88[0] | r88[1] << 8, 27504);
	/* dig_H4 / dig_H5 share a nibble at 0xE5 */
	zassert_equal((re1[3] << 4) | (re1[4] & 0x0F), 313);
	zassert_equal(((re1[4] >> 4) & 0x0F) | (re1[5] << 4), 50);
}

ZTEST_SUITE(sensors, NULL, NULL, NULL, NULL, NULL);
