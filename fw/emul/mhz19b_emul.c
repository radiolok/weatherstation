/* MH-Z19B emulator on uart-emul: answers 9-byte commands with checksums. */
#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/uart_emul.h>

#include <ws/sensors.h>

#include "emul.h"

static uint16_t ppm = 640;
static bool bad;
static uint8_t cmd[WS_MHZ19B_FRAME];
static size_t got;

void ws_emul_mhz19b_set(uint16_t value)
{
	ppm = value;
}

void ws_emul_mhz19b_fail(bool bad_checksum)
{
	bad = bad_checksum;
}

static void tx_ready(const struct device *dev, size_t size, const struct emul *target)
{
	uint8_t b;

	while (uart_emul_get_tx_data(dev, &b, 1) == 1) {
		if (got == 0 && b != 0xFF) {
			continue; /* resync on the start byte */
		}
		cmd[got++] = b;
		if (got < WS_MHZ19B_FRAME) {
			continue;
		}
		got = 0;
		uint8_t resp[WS_MHZ19B_FRAME];

		ws_mhz19b_response(resp, cmd[2], cmd[2] == WS_MHZ19B_CMD_READ ? ppm : 0);
		if (bad) {
			resp[8] ^= 0x55;
		}
		uart_emul_put_rx_data(dev, resp, sizeof(resp));
	}
}

static const struct uart_emul_device_api mhz_api = {
	.tx_data_ready = tx_ready,
};

static int mhz_init(const struct emul *target, const struct device *parent)
{
	return 0;
}

EMUL_DT_DEFINE(DT_ALIAS(ws_co2), mhz_init, NULL, NULL, &mhz_api, NULL);
