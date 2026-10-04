/*
 * RS-485 sniffer on the emulated sign UART: splits the byte stream into
 * Mobitec frames (FF ... FF) and decodes them with the same code as the
 * bench sniffer, so tests see what the sign would show.
 */
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include "emul.h"

static const struct device *const sign = DEVICE_DT_GET(DT_CHOSEN(ws_sign_uart));
static uint8_t buf[WS_MOBITEC_MAX + 8];
static size_t n;
static struct ws_frame frame;
static struct ws_emul_sign_stats stats;
K_SPINLOCK_DEFINE(sniff_lock);

static void on_tx(const struct device *dev, size_t size, void *user_data)
{
	uint8_t b;

	while (uart_emul_get_tx_data(dev, &b, 1) == 1) {
		k_spinlock_key_t key = k_spin_lock(&sniff_lock);

		stats.bytes++;
		if (n == 0 && b != 0xFF) {
			k_spin_unlock(&sniff_lock, key);
			continue;
		}
		if (n >= sizeof(buf)) {
			n = 0;
			stats.errors++;
		}
		buf[n++] = b;
		/* a frame ends with FF after at least the header and one band */
		if (b == 0xFF && n > 10) {
			struct ws_frame f;

			if (ws_mobitec_decode(buf, n, &f, NULL) == 0) {
				frame = f;
				stats.frames++;
				stats.last_ms = k_uptime_get();
			} else {
				stats.errors++;
			}
			n = 0;
		}
		k_spin_unlock(&sniff_lock, key);
	}
}

void ws_emul_sign_get(struct ws_frame *f, struct ws_emul_sign_stats *st)
{
	k_spinlock_key_t key = k_spin_lock(&sniff_lock);

	*f = frame;
	*st = stats;
	k_spin_unlock(&sniff_lock, key);
}

static int sniffer_init(void)
{
	uart_emul_callback_tx_data_ready_set(sign, on_tx, NULL);
	return 0;
}

SYS_INIT(sniffer_init, APPLICATION, 90);
