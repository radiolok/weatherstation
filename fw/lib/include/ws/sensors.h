/*
 * Sensor helpers without I/O: pressure trend over 3 h, MH-Z19B frames and the
 * BME280 compensation math (used by the native_sim emulator to produce raw
 * readings for given conditions).
 */
#ifndef WS_SENSORS_H_
#define WS_SENSORS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- pressure trend: 5 min averages over 3 h ---- */
#define WS_PTREND_SLOT_S 300
#define WS_PTREND_SLOTS  36 /* 3 h */

struct ws_ptrend {
	int64_t slot_start;
	int64_t sum;
	uint16_t n;
	uint8_t head;  /* next slot to write */
	uint8_t count; /* complete slots stored */
	int32_t hist[WS_PTREND_SLOTS + 1];
};

void ws_ptrend_init(struct ws_ptrend *t);
/* Adds a sample (tenths of mm Hg) at `now_s` (monotonic seconds). */
void ws_ptrend_add(struct ws_ptrend *t, int32_t deci, int64_t now_s);
/* Change over 3 h in tenths, false until 3 h of history exist. */
bool ws_ptrend_get(const struct ws_ptrend *t, int32_t *delta_deci);

/* hPa in hundredths (Pa) -> mm Hg in tenths */
int32_t ws_pa_to_mmhg_deci(int32_t pa);

/* ---- MH-Z19B ---- */
#define WS_MHZ19B_FRAME    9
#define WS_MHZ19B_CMD_READ 0x86

uint8_t ws_mhz19b_checksum(const uint8_t frame[WS_MHZ19B_FRAME]);
/* Response to "read CO2" with the given concentration. */
void ws_mhz19b_response(uint8_t out[WS_MHZ19B_FRAME], uint8_t cmd, uint16_t ppm);
/* 0 and ppm for a valid response, -1 for a bad frame or checksum. */
int ws_mhz19b_parse(const uint8_t in[WS_MHZ19B_FRAME], uint16_t *ppm);

/* ---- BME280 (datasheet, 4.2.3) ---- */
struct ws_bme280_calib {
	uint16_t t1;
	int16_t t2, t3;
	uint16_t p1;
	int16_t p2, p3, p4, p5, p6, p7, p8, p9;
	uint8_t h1, h3;
	int16_t h2, h4, h5;
	int8_t h6;
};

/* Typical calibration from the datasheet example, used by the emulator. */
extern const struct ws_bme280_calib ws_bme280_calib_example;

/* Register images: 0x88..0xA1 (26 bytes) and 0xE1..0xE7 (7 bytes). */
void ws_bme280_pack_calib(const struct ws_bme280_calib *c, uint8_t r88[26], uint8_t re1[7]);

/* Compensation like the Zephyr driver: temperature in 1/100 degC, pressure
 * in Pa as Q24.8, humidity in %RH as Q22.10. */
int32_t ws_bme280_temp(const struct ws_bme280_calib *c, int32_t adc_t, int32_t *t_fine);
uint32_t ws_bme280_press(const struct ws_bme280_calib *c, int32_t adc_p, int32_t t_fine);
uint32_t ws_bme280_hum(const struct ws_bme280_calib *c, int32_t adc_h, int32_t t_fine);

/* Raw ADC values that read back as the given conditions
 * (centi-degrees, Pa, %RH * 1024). */
void ws_bme280_raw_for(const struct ws_bme280_calib *c, int32_t centi_c, uint32_t pa,
		       uint32_t rh_q10, int32_t *adc_t, int32_t *adc_p, int32_t *adc_h);

#ifdef __cplusplus
}
#endif

#endif
