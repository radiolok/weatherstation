/* Pressure trend, MH-Z19B frames and BME280 math, see ws/sensors.h. */
#include <string.h>

#include <ws/sensors.h>

void ws_ptrend_init(struct ws_ptrend *t)
{
	memset(t, 0, sizeof(*t));
	t->slot_start = -1;
}

void ws_ptrend_add(struct ws_ptrend *t, int32_t deci, int64_t now_s)
{
	if (t->slot_start < 0) {
		t->slot_start = now_s;
	}
	/* close finished slots (gaps keep the last average) */
	while (now_s - t->slot_start >= WS_PTREND_SLOT_S) {
		int32_t avg = t->n ? (int32_t)(t->sum / t->n)
				   : (t->count ? t->hist[(t->head + WS_PTREND_SLOTS) %
							 (WS_PTREND_SLOTS + 1)]
					       : deci);

		t->hist[t->head] = avg;
		t->head = (uint8_t)((t->head + 1) % (WS_PTREND_SLOTS + 1));
		if (t->count < WS_PTREND_SLOTS + 1) {
			t->count++;
		}
		t->sum = 0;
		t->n = 0;
		t->slot_start += WS_PTREND_SLOT_S;
	}
	t->sum += deci;
	t->n++;
}

bool ws_ptrend_get(const struct ws_ptrend *t, int32_t *delta)
{
	if (t->count < WS_PTREND_SLOTS + 1) {
		return false;
	}
	int last = (t->head + WS_PTREND_SLOTS) % (WS_PTREND_SLOTS + 1);
	int first = t->head; /* oldest: 36 slots before the last one */

	*delta = t->hist[last] - t->hist[first];
	return true;
}

int32_t ws_pa_to_mmhg_deci(int32_t pa)
{
	/* 1 mm Hg = 133.322 Pa; tenths, rounded */
	int64_t v = (int64_t)pa * 1000000 / 1333224; /* hundredths */

	return (int32_t)((v + 5) / 10);
}

uint8_t ws_mhz19b_checksum(const uint8_t f[WS_MHZ19B_FRAME])
{
	uint8_t s = 0;

	for (int i = 1; i < 8; i++) {
		s += f[i];
	}
	return (uint8_t)(0xFF - s + 1);
}

void ws_mhz19b_response(uint8_t out[WS_MHZ19B_FRAME], uint8_t cmd, uint16_t ppm)
{
	memset(out, 0, WS_MHZ19B_FRAME);
	out[0] = 0xFF;
	out[1] = cmd;
	out[2] = (uint8_t)(ppm >> 8);
	out[3] = (uint8_t)ppm;
	out[4] = 0x40; /* temperature + 40, unused */
	out[8] = ws_mhz19b_checksum(out);
}

int ws_mhz19b_parse(const uint8_t in[WS_MHZ19B_FRAME], uint16_t *ppm)
{
	if (in[0] != 0xFF || in[1] != WS_MHZ19B_CMD_READ || in[8] != ws_mhz19b_checksum(in)) {
		return -1;
	}
	*ppm = (uint16_t)((in[2] << 8) | in[3]);
	return 0;
}

const struct ws_bme280_calib ws_bme280_calib_example = {
	.t1 = 27504,
	.t2 = 26435,
	.t3 = -1000,
	.p1 = 36477,
	.p2 = -10685,
	.p3 = 3024,
	.p4 = 2855,
	.p5 = 140,
	.p6 = -7,
	.p7 = 15500,
	.p8 = -14600,
	.p9 = 6000,
	.h1 = 75,
	.h2 = 370,
	.h3 = 0,
	.h4 = 313,
	.h5 = 50,
	.h6 = 30,
};

static void le16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

void ws_bme280_pack_calib(const struct ws_bme280_calib *c, uint8_t r88[26], uint8_t re1[7])
{
	const uint16_t v[12] = {c->t1,           (uint16_t)c->t2, (uint16_t)c->t3, c->p1,
				(uint16_t)c->p2, (uint16_t)c->p3, (uint16_t)c->p4, (uint16_t)c->p5,
				(uint16_t)c->p6, (uint16_t)c->p7, (uint16_t)c->p8, (uint16_t)c->p9};

	for (int i = 0; i < 12; i++) {
		le16(&r88[i * 2], v[i]);
	}
	r88[24] = 0;     /* 0xA0 reserved */
	r88[25] = c->h1; /* 0xA1 */
	le16(&re1[0], (uint16_t)c->h2);
	re1[2] = c->h3;
	re1[3] = (uint8_t)(c->h4 >> 4);
	re1[4] = (uint8_t)((c->h4 & 0x0F) | ((c->h5 & 0x0F) << 4));
	re1[5] = (uint8_t)(c->h5 >> 4);
	re1[6] = (uint8_t)c->h6;
}

int32_t ws_bme280_temp(const struct ws_bme280_calib *c, int32_t adc_t, int32_t *t_fine)
{
	int32_t var1 = (((adc_t >> 3) - ((int32_t)c->t1 << 1)) * ((int32_t)c->t2)) >> 11;
	int32_t var2 =
		(((((adc_t >> 4) - ((int32_t)c->t1)) * ((adc_t >> 4) - ((int32_t)c->t1))) >> 12) *
		 ((int32_t)c->t3)) >>
		14;

	*t_fine = var1 + var2;
	return (*t_fine * 5 + 128) >> 8;
}

uint32_t ws_bme280_press(const struct ws_bme280_calib *c, int32_t adc_p, int32_t t_fine)
{
	int64_t var1, var2, p;

	var1 = ((int64_t)t_fine) - 128000;
	var2 = var1 * var1 * (int64_t)c->p6;
	var2 = var2 + ((var1 * (int64_t)c->p5) * 131072);
	var2 = var2 + (((int64_t)c->p4) * 34359738368LL);
	var1 = ((var1 * var1 * (int64_t)c->p3) >> 8) + ((var1 * (int64_t)c->p2) * 4096);
	var1 = ((((int64_t)1) << 47) + var1) * ((int64_t)c->p1) >> 33;
	if (var1 == 0) {
		return 0;
	}
	p = 1048576 - adc_p;
	p = ((p * 2147483648LL - var2) * 3125) / var1;
	var1 = (((int64_t)c->p9) * (p >> 13) * (p >> 13)) >> 25;
	var2 = (((int64_t)c->p8) * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (((int64_t)c->p7) << 4);
	return (uint32_t)p;
}

uint32_t ws_bme280_hum(const struct ws_bme280_calib *c, int32_t adc_h, int32_t t_fine)
{
	int32_t h = t_fine - ((int32_t)76800);

	h = ((((adc_h << 14) - (((int32_t)c->h4) << 20) - (((int32_t)c->h5) * h)) +
	      ((int32_t)16384)) >>
	     15) *
	    (((((((h * ((int32_t)c->h6)) >> 10) *
		 (((h * ((int32_t)c->h3)) >> 11) + ((int32_t)32768))) >>
		10) +
	       ((int32_t)2097152)) *
		      ((int32_t)c->h2) +
	      8192) >>
	     14);
	h = (h - (((((h >> 15) * (h >> 15)) >> 7) * ((int32_t)c->h1)) >> 4));
	h = (h > 419430400 ? 419430400 : h);
	h = (h < 0 ? 0 : h);
	return (uint32_t)(h >> 12);
}

void ws_bme280_raw_for(const struct ws_bme280_calib *c, int32_t centi_c, uint32_t pa,
		       uint32_t rh_q10, int32_t *adc_t, int32_t *adc_p, int32_t *adc_h)
{
	int32_t lo, hi, tf = 0;

	/* temperature grows with adc_t */
	lo = 0;
	hi = (1 << 20) - 1;
	while (lo < hi) {
		int32_t mid = lo + (hi - lo) / 2;

		if (ws_bme280_temp(c, mid, &tf) < centi_c) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	*adc_t = lo;
	ws_bme280_temp(c, lo, &tf);

	/* pressure falls with adc_p */
	lo = 0;
	hi = (1 << 20) - 1;
	while (lo < hi) {
		int32_t mid = lo + (hi - lo) / 2;

		if ((ws_bme280_press(c, mid, tf) >> 8) > pa) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	*adc_p = lo;

	/* humidity grows with adc_h */
	lo = 0;
	hi = (1 << 16) - 1;
	while (lo < hi) {
		int32_t mid = lo + (hi - lo) / 2;

		if (ws_bme280_hum(c, mid, tf) < rh_q10) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	*adc_h = lo;
}
