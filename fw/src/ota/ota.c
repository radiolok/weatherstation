/*
 * Firmware update (plan F10): glue between the state machine in fw/lib/ota
 * and the device. Three ways in:
 *
 *   MQTT ws/<id>/ota {url, sha256, version} -> HTTP(S) download into slot 1
 *   web page POST /api/ota/upload           -> chunks written into slot 1
 *   MCUmgr (smpmgr/mcumgr) over UDP          -> Zephyr img_mgmt, the same slot
 *
 * After the hash check the slot is marked for a test boot and the board
 * reboots; MCUboot swaps the images. The new image confirms itself once it
 * reaches the broker; within WS_OTA_SELFTEST_S it otherwise reboots
 * unconfirmed and MCUboot reverts.
 *
 * On native_sim there is no bootloader: everything up to the mark and the
 * reboot runs against the slot in the simulated flash.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/zbus/zbus.h>

#include <ws/json.h>
#include <ws/ota.h>
#include <ws/util.h>

#include "app.h"
#include "mqtt/mqtt_svc.h"
#include "net/http_get.h"
#include "ota.h"
#include "services.h"

LOG_MODULE_REGISTER(ws_ota, LOG_LEVEL_INF);

#define SLOT1 FIXED_PARTITION_ID(slot1_partition)

static struct ws_ota o;
static K_MUTEX_DEFINE(lock);
static struct flash_img_context img;

static K_THREAD_STACK_DEFINE(ota_stack, CONFIG_WS_OTA_STACK_SIZE);
static struct k_work_q ota_q;
static void download_fn(struct k_work *w);
static void tick_fn(struct k_work *w);
static K_WORK_DEFINE(download_work, download_fn);
static K_WORK_DELAYABLE_DEFINE(tick_work, tick_fn);

static int64_t now_s(void)
{
	return ws_app_now().mono;
}

/* ---- actions ---- */

static void act(enum ws_ota_action a);

static int slot_open(void)
{
	int ret = boot_erase_img_bank(SLOT1);

	if (ret) {
		LOG_ERR("erase slot 1: %d", ret);
		return ret;
	}
	return flash_img_init_id(&img, SLOT1);
}

/* sha256 of what is really in the flash, not of what we meant to write */
static int slot_hash(size_t size, uint8_t out[32])
{
	const struct flash_area *fa;
	static uint8_t buf[256];
	struct ws_sha256 h;
	int ret = flash_area_open(SLOT1, &fa);

	if (ret) {
		return ret;
	}
	ws_sha256_init(&h);
	for (size_t off = 0; off < size && !ret; off += sizeof(buf)) {
		size_t n = MIN(sizeof(buf), size - off);

		ret = flash_area_read(fa, off, buf, n);
		ws_sha256_update(&h, buf, n);
	}
	flash_area_close(fa);
	ws_sha256_final(&h, out);
	return ret;
}

static int mark_slot(void)
{
	int ret = boot_request_upgrade(BOOT_UPGRADE_TEST);

	if (ret) {
		LOG_ERR("mark slot 1: %d", ret);
	}
	return ret;
}

static void act(enum ws_ota_action a)
{
	uint8_t sha[32];

	switch (a) {
	case WS_OTA_ACT_DOWNLOAD:
		k_work_submit_to_queue(&ota_q, &download_work);
		break;
	case WS_OTA_ACT_VERIFY: {
		int ret = flash_img_buffered_write(&img, NULL, 0, true);

		if (!ret) {
			ret = slot_hash(o.size, sha);
		}
		if (ret) {
			LOG_ERR("slot 1 flush/read: %d", ret);
			memset(sha, 0xff, sizeof(sha));
		}
		char hex[65];

		ws_hex(sha, 32, hex);
		LOG_INF("slot 1: %u bytes, sha256 %s", (unsigned)o.size, hex);
		act(ws_ota_verified(&o, sha, now_s()));
		if (o.state == WS_OTA_FAILED) {
			LOG_ERR("update rejected: %s", o.error);
		}
		break;
	}
	case WS_OTA_ACT_MARK:
		act(ws_ota_marked(&o, mark_slot(), now_s()));
		if (o.state == WS_OTA_READY) {
			LOG_INF("image %s ready, rebooting in %d s", o.version,
				WS_OTA_REBOOT_DELAY);
			k_work_reschedule_for_queue(&ota_q, &tick_work, K_SECONDS(1));
		}
		break;
	case WS_OTA_ACT_REBOOT:
		LOG_WRN("rebooting (%s)", o.error[0] ? o.error : "update");
		LOG_PANIC();
		sys_reboot(SYS_REBOOT_WARM);
		break;
	case WS_OTA_ACT_CONFIRM: {
		int ret = boot_write_img_confirmed();

		LOG_INF("image confirmed: %d", ret);
		break;
	}
	default:
		break;
	}
}

/* ---- download (MQTT) ---- */

static int body_cb(const uint8_t *data, size_t len, size_t total, void *user)
{
	int ret = flash_img_buffered_write(&img, data, len, false);

	if (ret) {
		return ret;
	}
	k_mutex_lock(&lock, K_FOREVER);
	ws_ota_progress(&o, len);
	k_mutex_unlock(&lock);
	return 0;
}

static void download_fn(struct k_work *w)
{
	static char url[sizeof(o.url)];
	static const sec_tag_t tags[] = {0x57530001}; /* the METAR root CAs */
	int ret;

	k_mutex_lock(&lock, K_FOREVER);
	strcpy(url, o.url);
	k_mutex_unlock(&lock);
	LOG_INF("download %s", url);
	ret = slot_open();
	if (!ret) {
		struct ws_http_get req = {
			.url = url,
			.tags = tags,
			.n_tags = ARRAY_SIZE(tags),
			.timeout_ms = 30000,
			.body = body_cb,
		};

		ret = ws_http_get(&req);
	}
	k_mutex_lock(&lock, K_FOREVER);
	act(ws_ota_download_done(&o, ret, now_s()));
	if (o.state == WS_OTA_FAILED) {
		LOG_ERR("update failed: %s", o.error);
	}
	k_mutex_unlock(&lock);
}

/* ---- timers and self-test ---- */

static void tick_fn(struct k_work *w)
{
	k_mutex_lock(&lock, K_FOREVER);
	enum ws_ota_action a = ws_ota_tick(&o, now_s());
	bool again = o.deadline != 0;

	k_mutex_unlock(&lock);
	act(a);
	if (again) {
		k_work_reschedule_for_queue(&ota_q, &tick_work, K_SECONDS(1));
	}
}

static void mqtt_listener_cb(const struct zbus_channel *chan)
{
	const struct ws_msg_mqtt *m = zbus_chan_const_msg(chan);

	if (!m->connected) {
		return;
	}
	k_mutex_lock(&lock, K_FOREVER);
	enum ws_ota_action a = ws_ota_mqtt_connected(&o, now_s());

	k_mutex_unlock(&lock);
	act(a);
}
ZBUS_LISTENER_DEFINE(ws_ota_mqtt_lis, mqtt_listener_cb);
ZBUS_CHAN_ADD_OBS(ws_chan_mqtt, ws_ota_mqtt_lis, 4);

#ifdef CONFIG_WS_SMP
#include "smp_udp.h"

/* MCUmgr over UDP opens once the network is up (development builds) */
static void smp_open_fn(struct k_work *w)
{
	static bool opened;

	if (!opened) {
		int ret = ws_smp_udp_open(CONFIG_WS_SMP_PORT);

		opened = ret == 0;
		LOG_INF("SMP over UDP port %d: %d", CONFIG_WS_SMP_PORT, ret);
	}
}
static K_WORK_DEFINE(smp_open_work, smp_open_fn);

static void net_listener_cb(const struct zbus_channel *chan)
{
	const struct ws_msg_net *m = zbus_chan_const_msg(chan);

	if (m->status == WS_NETST_ONLINE || m->status == WS_NETST_AP) {
		k_work_submit_to_queue(&ota_q, &smp_open_work);
	}
}
ZBUS_LISTENER_DEFINE(ws_ota_net_lis, net_listener_cb);
ZBUS_CHAN_ADD_OBS(ws_chan_net, ws_ota_net_lis, 4);
#endif

static void start_selftest(uint32_t seconds)
{
	k_mutex_lock(&lock, K_FOREVER);
	o.selftest_s = seconds;
	ws_ota_boot(&o, false, now_s());
	k_mutex_unlock(&lock);
	LOG_WRN("unconfirmed image: self-test, %u s to reach the broker", seconds);
#ifdef CONFIG_WS_MQTT
	if (ws_mqtt_connected()) {
		/* the broker is already there (bench test): pass at once */
		k_mutex_lock(&lock, K_FOREVER);
		enum ws_ota_action a = ws_ota_mqtt_connected(&o, now_s());

		k_mutex_unlock(&lock);
		act(a);
		return;
	}
#endif
	k_work_reschedule_for_queue(&ota_q, &tick_work, K_SECONDS(1));
}

int ws_ota_boot_check(void)
{
	ws_ota_init(&o);
	k_work_queue_start(&ota_q, ota_stack, K_THREAD_STACK_SIZEOF(ota_stack),
			   K_LOWEST_APPLICATION_THREAD_PRIO,
			   &(struct k_work_queue_config){.name = "ota"});
	/* REVERT: MCUboot swapped in a test image that is not confirmed yet */
	if (mcuboot_swap_type() == BOOT_SWAP_TYPE_REVERT) {
		start_selftest(WS_OTA_SELFTEST_S);
	} else if (!boot_is_img_confirmed()) {
		/* first boot after flashing: nothing to revert to */
		boot_write_img_confirmed();
	}
	return 0;
}

/* ---- API ---- */

int ws_ota_request_json(const char *json, size_t len)
{
	k_mutex_lock(&lock, K_FOREVER);
	enum ws_ota_action a = ws_ota_request(&o, json, len, now_s());

	if (a == WS_OTA_ACT_NONE) {
		LOG_WRN("OTA request refused: %s", o.error);
	}
	k_mutex_unlock(&lock);
	act(a);
	return a == WS_OTA_ACT_DOWNLOAD ? 0 : (o.state == WS_OTA_FAILED ? -EINVAL : -EBUSY);
}

int ws_ota_web_begin(size_t total, const uint8_t *sha256)
{
	int ret = -EBUSY;

	k_mutex_lock(&lock, K_FOREVER);
	ws_ota_upload_begin(&o, WS_OTA_SRC_WEB, total, sha256, now_s());
	if (o.state == WS_OTA_DOWNLOAD && o.source == WS_OTA_SRC_WEB) {
		ret = slot_open();
		if (ret) {
			ws_ota_download_done(&o, ret, now_s());
		}
	} else if (o.state == WS_OTA_FAILED) {
		ret = -EINVAL;
	}
	k_mutex_unlock(&lock);
	return ret;
}

int ws_ota_web_chunk(const uint8_t *data, size_t len)
{
	int ret;

	k_mutex_lock(&lock, K_FOREVER);
	if (o.state != WS_OTA_DOWNLOAD || o.source != WS_OTA_SRC_WEB) {
		k_mutex_unlock(&lock);
		return -EINVAL;
	}
	ret = flash_img_buffered_write(&img, data, len, false);
	if (ret) {
		act(ws_ota_download_done(&o, ret, now_s()));
	} else {
		/* the last chunk switches to VERIFY by size */
		act(ws_ota_progress(&o, len));
	}
	k_mutex_unlock(&lock);
	return ret;
}

int ws_ota_web_finish(int err)
{
	k_mutex_lock(&lock, K_FOREVER);
	if (o.source == WS_OTA_SRC_WEB) {
		act(ws_ota_download_done(&o, err, now_s()));
	}
	int ret = o.state == WS_OTA_READY ? 0 : -EIO;

	k_mutex_unlock(&lock);
	return ret;
}

const char *ws_ota_state_str(void)
{
	return ws_ota_state_name(o.state);
}

int ws_ota_status_json(char *buf, size_t len)
{
	struct ws_jw w;

	k_mutex_lock(&lock, K_FOREVER);
	ws_jw_init(&w, buf, len);
	ws_jw_obj(&w);
	ws_jw_kstr(&w, "state", ws_ota_state_name(o.state));
	ws_jw_kint(&w, "progress", ws_ota_percent(&o));
	ws_jw_kint(&w, "bytes", o.size);
	ws_jw_kstr(&w, "version", o.version);
	ws_jw_kstr(&w, "error", o.error);
	ws_jw_kbool(&w, "confirmed", o.confirmed);
	ws_jw_obj_end(&w);
	k_mutex_unlock(&lock);
	return w.ok ? (int)w.pos : -1;
}

/* ---- shell ---- */

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	k_mutex_lock(&lock, K_FOREVER);
	shell_print(sh, "state: %s", ws_ota_state_name(o.state));
	shell_print(sh, "bytes: %u", (unsigned)o.size);
	shell_print(sh, "version: %s", o.version[0] ? o.version : "-");
	shell_print(sh, "error: %s", o.error[0] ? o.error : "-");
	shell_print(sh, "confirmed: %s", o.confirmed ? "yes" : "no");
	shell_print(sh, "swap_type: %d", mcuboot_swap_type());
	k_mutex_unlock(&lock);
	return 0;
}

/* ws ota get <url> <sha256> [version] : same as the MQTT command */
static int cmd_get(const struct shell *sh, size_t argc, char **argv)
{
	static char json[sizeof(o.url) + 128];

	snprintk(json, sizeof(json), "{\"url\":\"%s\",\"sha256\":\"%s\",\"version\":\"%s\"}",
		 argv[1], argv[2], argc > 3 ? argv[3] : "");
	int ret = ws_ota_request_json(json, strlen(json));

	shell_print(sh, "request: %d", ret);
	return ret ? -ENOEXEC : 0;
}

/* ws ota selftest [s] : behave as after a test boot (bench test of the
 * self-test without MCUboot) */
static int cmd_selftest(const struct shell *sh, size_t argc, char **argv)
{
	start_selftest(argc > 1 ? (uint32_t)atoi(argv[1]) : WS_OTA_SELFTEST_S);
	shell_print(sh, "self-test started");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	ota_cmds, SHELL_CMD_ARG(status, NULL, "Update state", cmd_status, 1, 0),
	SHELL_CMD_ARG(get, NULL, "Download: get <url> <sha256> [version]", cmd_get, 3, 1),
	SHELL_CMD_ARG(selftest, NULL, "Start the self-test [seconds]", cmd_selftest, 1, 1),
	SHELL_SUBCMD_SET_END);
SHELL_SUBCMD_ADD((ws), ota, &ota_cmds, "Firmware update", NULL, 0, 0);
