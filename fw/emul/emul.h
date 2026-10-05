/* native_sim emulators: values the tests set through "ws emul ..." */
#ifndef WS_EMUL_H_
#define WS_EMUL_H_

#include <stdbool.h>
#include <stdint.h>

#include <ws/sign.h>

void ws_emul_bme280_set(int32_t centi_c, uint32_t pa, uint32_t rh_percent_x10);
void ws_emul_bme280_fail(bool fail);
void ws_emul_mhz19b_set(uint16_t ppm);
void ws_emul_mhz19b_fail(bool bad_checksum);

/* Sign sniffer on the RS-485 UART: decoded frames */
struct ws_emul_sign_stats {
	uint32_t frames;
	uint32_t errors;
	uint32_t bytes;
	int64_t last_ms;
};
void ws_emul_sign_get(struct ws_frame *f, struct ws_emul_sign_stats *st);

#endif
