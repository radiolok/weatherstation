/*
 * Sign service (fw/README.md, "Табло"): every 10 s and on every variable
 * change the rules pick a screen, the renderer draws it, a changed frame
 * goes to the sign at once and an unchanged one is repeated every 30 s so
 * the sign recovers after a power loss.
 */
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <ws/tz.h>

#include "app.h"
#include "display.h"

LOG_MODULE_REGISTER(ws_display, LOG_LEVEL_INF);

#define SIGN_UART DT_CHOSEN(ws_sign_uart)

static const struct device *const uart = DEVICE_DT_GET(SIGN_UART);

static struct k_work_delayable work;
static struct ws_frame last; /* last frame sent */
static bool have_last;
static int64_t last_tx_ms;
static struct ws_display_stats stats;
static uint32_t flips_hour[24];
static int64_t flips_hour_start;
static int flips_slot;
static enum ws_sign_pattern pattern;
static char last_state_key[WS_ID_LEN + 96];

/* preview of a configuration that is not saved */
static WS_BIG_BSS struct ws_config preview_cfg;
static int preview_screen = -1;
static int64_t preview_until_ms;

K_MUTEX_DEFINE(disp_lock);

/* ---- transmitter: one frame at a time, 4800 baud takes ~0.7 s ---- */

static uint8_t tx_buf[WS_MOBITEC_MAX];
static size_t tx_len;
K_SEM_DEFINE(tx_sem, 0, 1);
K_MUTEX_DEFINE(tx_lock);

static void tx_thread(void *a, void *b, void *c)
{
	uint8_t frame[WS_MOBITEC_MAX];
	size_t n;

	for (;;) {
		k_sem_take(&tx_sem, K_FOREVER);
		k_mutex_lock(&tx_lock, K_FOREVER);
		n = tx_len;
		memcpy(frame, tx_buf, n);
		k_mutex_unlock(&tx_lock);
		for (size_t i = 0; i < n; i++) {
			uart_poll_out(uart, frame[i]);
		}
		stats.tx_bytes += n;
	}
}

K_THREAD_DEFINE(ws_sign_tx, 1024, tx_thread, NULL, NULL, NULL, 6, 0, 0);

static void send(const struct ws_frame *f)
{
	k_mutex_lock(&tx_lock, K_FOREVER);
	tx_len = ws_mobitec_encode(f, WS_MOBITEC_ADDR, tx_buf, sizeof(tx_buf));
	k_mutex_unlock(&tx_lock);
	k_sem_give(&tx_sem); /* the newest frame wins if one is still queued */
	last_tx_ms = k_uptime_get();
}

static void draw_pattern(enum ws_sign_pattern p, struct ws_frame *f)
{
	ws_frame_clear(f);
	for (int y = 0; y < WS_H; y++) {
		for (int x = 0; x < WS_W; x++) {
			bool on = false;

			switch (p) {
			case WS_PATTERN_CHECKER:
				on = (x + y) % 2 == 0;
				break;
			case WS_PATTERN_COLUMNS:
				on = x % 2 == 0;
				break;
			case WS_PATTERN_ROWS:
				on = y % 2 == 0;
				break;
			case WS_PATTERN_ALL:
				on = true;
				break;
			default:
				break;
			}
			ws_frame_set(f, x, y, on);
		}
	}
}

static void count_flips(uint32_t dots)
{
	int64_t now = k_uptime_get() / 1000;

	if (now - flips_hour_start >= 3600) {
		int steps = (int)((now - flips_hour_start) / 3600);

		for (int i = 0; i < steps && i < 24; i++) {
			flips_slot = (flips_slot + 1) % 24;
			flips_hour[flips_slot] = 0;
		}
		flips_hour_start += (int64_t)steps * 3600;
	}
	flips_hour[flips_slot] += dots;
	stats.flips_total += dots;
	uint32_t sum = 0;

	for (int i = 0; i < 24; i++) {
		sum += flips_hour[i];
	}
	stats.flips_24h = sum;
}

static int32_t local_sod(void)
{
	struct ws_tm tm;

	if (!ws_app_localtime(&tm)) {
		return -1;
	}
	return tm.hour * 3600 + tm.min * 60 + tm.sec;
}

/* Delay until the next rotator boundary of the screen, capped by the period. */
static int32_t next_wake_ms(const struct ws_config *cfg, int screen, int32_t sod)
{
	int32_t best = WS_DISPLAY_PERIOD_MS;
	int64_t t = sod >= 0 ? sod : k_uptime_get() / 1000;

	if (screen < 0 || screen >= cfg->n_screens) {
		return best;
	}
	const struct ws_screen *sc = &cfg->screens[screen];

	for (int i = 0; i < sc->n_items; i++) {
		const struct ws_item *it = &cfg->items[sc->items + i];

		if (it->type != WS_IT_ROTATOR) {
			continue;
		}
		int64_t into = ((t - it->offset) % it->period + it->period) % it->period;
		int32_t ms = (int32_t)(it->period - into) * 1000 + 50;

		if (ms < best) {
			best = ms;
		}
	}
	return best;
}

static void publish_state(const struct ws_engine *e, const struct ws_config *cfg)
{
	struct ws_msg_display m = {0};
	const struct ws_screen *sc = &cfg->screens[e->current];
	char key[sizeof(last_state_key)];

	snprintf(key, sizeof(key), "%s|%s|%d", sc->id, e->reason, e->pinned >= 0);
	if (strcmp(key, last_state_key) == 0) {
		return;
	}
	strcpy(last_state_key, key);
	snprintf(m.screen, sizeof(m.screen), "%s", sc->id);
	snprintf(m.name, sizeof(m.name), "%s", sc->name);
	snprintf(m.reason, sizeof(m.reason), "%s", e->reason);
	m.pinned = e->pinned >= 0;
	m.flips_24h = stats.flips_24h;
	zbus_chan_pub(&ws_chan_display, &m, K_MSEC(50));
	LOG_INF("screen %s (%s)", sc->id, e->reason);
}

static void display_step(struct k_work *w)
{
	static struct ws_frame f;
	struct ws_now now = ws_app_now();
	int32_t sod = local_sod();
	int32_t wake = WS_DISPLAY_PERIOD_MS;
	bool engine_ran = false;

	k_mutex_lock(&disp_lock, K_FOREVER);
	if (pattern != WS_PATTERN_NONE) {
		draw_pattern(pattern, &f);
	} else if (preview_screen >= 0 && k_uptime_get() < preview_until_ms) {
		struct ws_render_ctx rc;

		ws_app_lock();
		rc = (struct ws_render_ctx){&preview_cfg, ws_app_vars(), now, sod};
		ws_render_screen(&rc, preview_screen, &f);
		ws_app_unlock();
		wake = (int32_t)MIN(preview_until_ms - k_uptime_get() + 10, WS_DISPLAY_PERIOD_MS);
	} else {
		preview_screen = -1;
		ws_app_lock();
		struct ws_engine *e = ws_app_engine();
		struct ws_config *cfg = ws_app_cfg();

		ws_engine_step(e, ws_app_vars(), &now);
		struct ws_render_ctx rc = {cfg, ws_app_vars(), now, sod};

		ws_render_screen(&rc, e->current, &f);
		wake = next_wake_ms(cfg, e->current, sod);
		publish_state(e, cfg);
		ws_app_unlock();
		engine_ran = true;
	}

	if (!have_last || memcmp(f.bits, last.bits, sizeof(f.bits)) != 0) {
		struct ws_frame_diff d;

		if (have_last) {
			d = ws_frame_diff(&last, &f);
		} else {
			struct ws_frame blank;

			ws_frame_clear(&blank);
			d = ws_frame_diff(&blank, &f);
		}
		stats.last_dots = d.dots;
		stats.last_cols = d.cols;
		count_flips(d.dots);
		stats.frames_sent++;
		last = f;
		have_last = true;
		send(&f);
	} else if (k_uptime_get() - last_tx_ms >= WS_DISPLAY_REPEAT_MS) {
		stats.frames_repeated++;
		send(&f);
	}
	int32_t to_repeat = (int32_t)(last_tx_ms + WS_DISPLAY_REPEAT_MS - k_uptime_get());

	if (to_repeat > 0 && to_repeat < wake) {
		wake = to_repeat;
	}
	k_mutex_unlock(&disp_lock);
	(void)engine_ran;
	k_work_reschedule(&work, K_MSEC(MAX(wake, 100)));
}

/* Variables changed: re-evaluate soon (coalesces bursts). */
static void on_vars(const struct zbus_channel *chan)
{
	k_work_reschedule(&work, K_MSEC(200));
}

ZBUS_LISTENER_DEFINE(ws_display_vars_lis, on_vars);
ZBUS_CHAN_ADD_OBS(ws_chan_vars, ws_display_vars_lis, 3);
ZBUS_CHAN_ADD_OBS(ws_chan_cfg, ws_display_vars_lis, 3);

static void on_button(const struct zbus_channel *chan)
{
	const struct ws_msg_button *m = zbus_chan_const_msg(chan);

	if (m->event == WS_BTNMSG_SHORT) {
		struct ws_now now = ws_app_now();

		ws_app_lock();
		ws_engine_debug_next(ws_app_engine(), now.mono);
		ws_app_unlock();
		k_work_reschedule(&work, K_NO_WAIT);
	}
}

ZBUS_LISTENER_DEFINE(ws_display_btn_lis, on_button);
ZBUS_CHAN_ADD_OBS(ws_chan_button, ws_display_btn_lis, 3);

void ws_display_kick(void)
{
	k_work_reschedule(&work, K_NO_WAIT);
}

void ws_display_pin(int screen, uint32_t minutes)
{
	struct ws_now now = ws_app_now();

	ws_app_lock();
	ws_engine_pin(ws_app_engine(), screen, minutes, now.mono);
	ws_app_unlock();
	ws_display_kick();
}

int ws_display_preview(const char *json, size_t len, const char *screen_id, uint32_t seconds,
		       struct ws_jtok *toks, struct ws_cfg_errors *errs)
{
	int r, s = 0;

	if (seconds == 0 || seconds > 300) {
		seconds = 60;
	}
	k_mutex_lock(&disp_lock, K_FOREVER);
	r = ws_cfg_compile(json, len, &preview_cfg, errs, toks, WS_JSON_TOKENS);
	if (r == 0 && screen_id) {
		s = ws_cfg_screen_by_id(&preview_cfg, screen_id);
		if (s < 0) {
			errs->count = 1;
			snprintf(errs->e[0].path, sizeof(errs->e[0].path), "screen");
			snprintf(errs->e[0].msg, sizeof(errs->e[0].msg), "нет экрана «%s»",
				 screen_id);
			r = -1;
		}
	}
	if (r == 0) {
		preview_screen = s;
		preview_until_ms = k_uptime_get() + (int64_t)seconds * 1000;
	}
	k_mutex_unlock(&disp_lock);
	if (r == 0) {
		ws_display_kick();
	}
	return r;
}

void ws_display_pattern(enum ws_sign_pattern p)
{
	k_mutex_lock(&disp_lock, K_FOREVER);
	pattern = p;
	k_mutex_unlock(&disp_lock);
	ws_display_kick();
}

int ws_display_pattern_parse(const char *s)
{
	static const char *const names[] = {"auto", "checker", "columns", "rows", "all", "none"};

	for (int i = 0; i < (int)ARRAY_SIZE(names); i++) {
		if (strcmp(s, names[i]) == 0) {
			return i;
		}
	}
	return -1;
}

void ws_display_get_stats(struct ws_display_stats *st)
{
	k_mutex_lock(&disp_lock, K_FOREVER);
	*st = stats;
	k_mutex_unlock(&disp_lock);
}

void ws_display_last_frame(struct ws_frame *f)
{
	k_mutex_lock(&disp_lock, K_FOREVER);
	*f = last;
	k_mutex_unlock(&disp_lock);
}

int ws_display_start(void)
{
	if (!device_is_ready(uart)) {
		LOG_ERR("sign UART not ready");
		return -ENODEV;
	}
	flips_hour_start = k_uptime_get() / 1000;
	k_work_init_delayable(&work, display_step);
	k_work_reschedule(&work, K_MSEC(100));
	return 0;
}
