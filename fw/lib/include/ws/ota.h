/*
 * Firmware update state machine (plan F10, fw/README.md "OTA").
 *
 *   idle --request--> download --done--> verify --sha256 ok--> ready
 *     ready: the service marks slot 1 for a test boot and reboots
 *   boot of an unconfirmed image --> testing (120 s)
 *     testing --MQTT connected--> confirm --> idle (confirmed)
 *     testing --timeout--> reboot without confirming: MCUboot reverts
 *   any error --> failed (reason kept), a new request is accepted
 *
 * Pure logic: the service performs the returned actions (download, write,
 * hash, mark, reboot, confirm) and feeds back events. Times are monotonic
 * seconds.
 */
#ifndef WS_OTA_LIB_H_
#define WS_OTA_LIB_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS_OTA_SELFTEST_S   120 /* time to reach the broker after a test boot */
#define WS_OTA_REBOOT_DELAY 3   /* seconds to report "ready" before rebooting */

enum ws_ota_state {
	WS_OTA_IDLE,
	WS_OTA_DOWNLOAD,
	WS_OTA_VERIFY,
	WS_OTA_READY,
	WS_OTA_TESTING,
	WS_OTA_FAILED,
};

enum ws_ota_action {
	WS_OTA_ACT_NONE,
	WS_OTA_ACT_DOWNLOAD, /* start fetching ota.url into slot 1 */
	WS_OTA_ACT_VERIFY,   /* hash slot 1 over `size` bytes and report */
	WS_OTA_ACT_MARK,     /* request a test boot of slot 1 */
	WS_OTA_ACT_REBOOT,
	WS_OTA_ACT_CONFIRM, /* mark the running image as good */
};

enum ws_ota_source {
	WS_OTA_SRC_MQTT,
	WS_OTA_SRC_WEB,
	WS_OTA_SRC_SMP,
};

struct ws_ota {
	enum ws_ota_state state;
	enum ws_ota_source source;
	char url[192];
	char version[24];
	uint8_t sha256[32];
	bool check_sha; /* false: no hash given (web upload over HTTP, SMP) */
	size_t size;    /* bytes received */
	size_t total;   /* expected size, 0 unknown */
	int64_t since;  /* when the state was entered */
	int64_t deadline;
	char error[64];      /* reason of the last failure (UTF-8) */
	bool confirmed;      /* the running image is confirmed */
	uint32_t selftest_s; /* WS_OTA_SELFTEST_S, shorter in bench tests */
};

void ws_ota_init(struct ws_ota *o);

/* MQTT command {"url": "http(s)://...", "sha256": "<64 hex>", "version": "..."}.
 * Returns the action (DOWNLOAD) or NONE with o->error set (busy, bad fields). */
enum ws_ota_action ws_ota_request(struct ws_ota *o, const char *json, size_t len, int64_t now);

/* Upload through the web page or SMP: the image comes in pieces. */
enum ws_ota_action ws_ota_upload_begin(struct ws_ota *o, enum ws_ota_source src, size_t total,
				       const uint8_t sha256[32], int64_t now);

/* Bytes written to slot 1. Returns NONE, or VERIFY when `total` is reached. */
enum ws_ota_action ws_ota_progress(struct ws_ota *o, size_t len);
/* Transfer finished (err 0) or failed (negative errno). */
enum ws_ota_action ws_ota_download_done(struct ws_ota *o, int err, int64_t now);
/* Hash of slot 1 over o->size bytes. */
enum ws_ota_action ws_ota_verified(struct ws_ota *o, const uint8_t sha256[32], int64_t now);
/* The slot was marked (err 0) or marking failed. */
enum ws_ota_action ws_ota_marked(struct ws_ota *o, int err, int64_t now);

/* After start-up: is the running image confirmed? */
enum ws_ota_action ws_ota_boot(struct ws_ota *o, bool confirmed, int64_t now);
/* The broker connection is the self-test. */
enum ws_ota_action ws_ota_mqtt_connected(struct ws_ota *o, int64_t now);
/* Periodic: reboot timers. */
enum ws_ota_action ws_ota_tick(struct ws_ota *o, int64_t now);

const char *ws_ota_state_name(enum ws_ota_state s);
/* Percent of the download, -1 when the size is unknown. */
int ws_ota_percent(const struct ws_ota *o);

#ifdef __cplusplus
}
#endif

#endif
