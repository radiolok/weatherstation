/*
 * BME280 and MH-Z19B through the sensor API, every 30 s (F5). Pressure trend
 * over 3 h is computed in fw/lib/sensors.
 */
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <ws/sensors.h>

#include "app.h"

LOG_MODULE_REGISTER(ws_sensors, LOG_LEVEL_INF);

#define PERIOD_S 30

static const struct device *const thp = DEVICE_DT_GET(DT_ALIAS(ws_thp));
static const struct device *const co2 = DEVICE_DT_GET(DT_ALIAS(ws_co2));

static struct ws_ptrend trend;
static uint32_t thp_errors, co2_errors, reads;
static int32_t last_t, last_rh, last_p, last_co2;
static bool thp_ok, co2_ok;

static int32_t to_deci(const struct sensor_value *v)
{
	int64_t micro = (int64_t)v->val1 * 1000000 + v->val2;

	return (int32_t)((micro >= 0 ? micro + 50000 : micro - 50000) / 100000);
}

static void read_thp(const struct ws_settings *s, int64_t mono)
{
	struct sensor_value t, p, h;

	if (!device_is_ready(thp) || sensor_sample_fetch(thp) ||
	    sensor_channel_get(thp, SENSOR_CHAN_AMBIENT_TEMP, &t) ||
	    sensor_channel_get(thp, SENSOR_CHAN_PRESS, &p) ||
	    sensor_channel_get(thp, SENSOR_CHAN_HUMIDITY, &h)) {
		thp_errors++;
		thp_ok = false;
		return;
	}
	/* pressure in kPa -> Pa */
	int32_t pa = p.val1 * 1000 + p.val2 / 1000;

	last_t = to_deci(&t) + s->t_offset;
	last_rh = to_deci(&h) + s->rh_offset;
	last_rh = CLAMP(last_rh, 0, 1000);
	last_p = ws_pa_to_mmhg_deci(pa);
	thp_ok = true;
	ws_ptrend_add(&trend, last_p, mono);

	ws_app_lock();
	struct ws_vars *v = ws_app_vars();
	int32_t d;

	ws_vars_set_num(v, WS_V_IN_T, last_t, mono);
	ws_vars_set_num(v, WS_V_IN_RH, last_rh, mono);
	ws_vars_set_num(v, WS_V_IN_P, last_p, mono);
	if (ws_ptrend_get(&trend, &d)) {
		ws_vars_set_num(v, WS_V_IN_P_TREND, d, mono);
	}
	ws_app_unlock();
}

static void read_co2(int64_t mono)
{
	struct sensor_value c;

	if (!device_is_ready(co2) || sensor_sample_fetch(co2) ||
	    sensor_channel_get(co2, SENSOR_CHAN_CO2, &c)) {
		co2_errors++;
		co2_ok = false;
		return;
	}
	last_co2 = c.val1;
	co2_ok = true;
	ws_app_lock();
	ws_vars_set_num(ws_app_vars(), WS_V_IN_CO2, last_co2 * 10, mono);
	ws_app_unlock();
}

static void sensors_thread(void *a, void *b, void *c)
{
	struct ws_settings s;

	ws_ptrend_init(&trend);
	for (;;) {
		int64_t mono = ws_app_now().mono;

		ws_app_settings_get(&s);
		read_thp(&s, mono);
		read_co2(mono);
		reads++;
		ws_app_vars_touched();
		k_sleep(K_SECONDS(PERIOD_S));
	}
}

K_THREAD_DEFINE(ws_sensors_tid, 2048, sensors_thread, NULL, NULL, NULL, 10, 0, 1000);

static void print_deci(const struct shell *sh, const char *name, bool ok, int32_t v,
		       const char *unit)
{
	if (ok) {
		shell_print(sh, "%s: %s%d.%d %s", name, v < 0 ? "-" : "", abs(v) / 10, abs(v) % 10,
			    unit);
	} else {
		shell_print(sh, "%s: --", name);
	}
}

static int cmd_sensors(const struct shell *sh, size_t argc, char **argv)
{
	int32_t d;

	print_deci(sh, "t", thp_ok, last_t, "C");
	print_deci(sh, "rh", thp_ok, last_rh, "%");
	print_deci(sh, "p", thp_ok, last_p, "mmHg");
	if (ws_ptrend_get(&trend, &d)) {
		print_deci(sh, "ptrend", true, d, "mm/3h");
	} else {
		shell_print(sh, "ptrend: --");
	}
	if (co2_ok) {
		shell_print(sh, "co2: %d ppm", last_co2);
	} else {
		shell_print(sh, "co2: --");
	}
	shell_print(sh, "reads: %u", reads);
	shell_print(sh, "thp_errors: %u", thp_errors);
	shell_print(sh, "co2_errors: %u", co2_errors);
	return 0;
}

SHELL_SUBCMD_ADD((ws), sensors, NULL, "Last sensor readings", cmd_sensors, 1, 0);
