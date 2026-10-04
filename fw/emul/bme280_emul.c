/*
 * BME280 emulator on the i2c-emul bus (Zephyr 4.3 has none): register map
 * with the datasheet calibration; raw readings are computed back from the
 * requested temperature, pressure and humidity with fw/lib/sensors.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/kernel.h>

#include <ws/sensors.h>

#include "emul.h"

static uint8_t regs[256];
static bool failing;

static void update_data(int32_t centi_c, uint32_t pa, uint32_t rh_q10)
{
	int32_t at, ap, ah;

	ws_bme280_raw_for(&ws_bme280_calib_example, centi_c, pa, rh_q10, &at, &ap, &ah);
	regs[0xF7] = (uint8_t)(ap >> 12);
	regs[0xF8] = (uint8_t)(ap >> 4);
	regs[0xF9] = (uint8_t)((ap & 0xF) << 4);
	regs[0xFA] = (uint8_t)(at >> 12);
	regs[0xFB] = (uint8_t)(at >> 4);
	regs[0xFC] = (uint8_t)((at & 0xF) << 4);
	regs[0xFD] = (uint8_t)(ah >> 8);
	regs[0xFE] = (uint8_t)ah;
}

void ws_emul_bme280_set(int32_t centi_c, uint32_t pa, uint32_t rh_percent_x10)
{
	update_data(centi_c, pa, rh_percent_x10 * 1024 / 10);
}

void ws_emul_bme280_fail(bool fail)
{
	failing = fail;
}

static int bme_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs, int addr)
{
	if (failing) {
		return -EIO;
	}
	/* write [reg] (+ value) then optional read */
	if (num_msgs < 1 || (msgs[0].flags & I2C_MSG_READ) || msgs[0].len < 1) {
		return -EIO;
	}
	uint8_t reg = msgs[0].buf[0];

	if (msgs[0].len == 2) {
		/* writes to reset/ctrl/config are accepted and ignored */
		regs[reg] = reg == 0xE0 ? 0 : msgs[0].buf[1];
	}
	for (int i = 1; i < num_msgs; i++) {
		if (!(msgs[i].flags & I2C_MSG_READ)) {
			return -EIO;
		}
		for (uint32_t k = 0; k < msgs[i].len; k++) {
			msgs[i].buf[k] = regs[(uint8_t)(reg + k)];
		}
		reg += msgs[i].len;
	}
	return 0;
}

static const struct i2c_emul_api bme_api = {
	.transfer = bme_transfer,
};

static int bme_init(const struct emul *target, const struct device *parent)
{
	uint8_t r88[26], re1[7];

	ws_bme280_pack_calib(&ws_bme280_calib_example, r88, re1);
	memcpy(&regs[0x88], r88, sizeof(r88));
	memcpy(&regs[0xE1], re1, sizeof(re1));
	regs[0xD0] = 0x60; /* chip id */
	regs[0xF3] = 0;    /* status: never measuring */
	ws_emul_bme280_set(2215, 99725, 410);
	return 0;
}

EMUL_DT_DEFINE(DT_ALIAS(ws_thp), bme_init, NULL, NULL, &bme_api, NULL);
