/* Firmware update over the network (F10): MQTT command, web upload, SMP. */
#ifndef WS_OTA_H_
#define WS_OTA_H_

#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_WS_OTA
/* {"url": "http://...", "sha256": "<64 hex>", "version": "1.2.3"} from MQTT
 * or the shell. 0 when the download starts, else -EBUSY/-EINVAL. */
int ws_ota_request_json(const char *json, size_t len);
/* Upload through the web page: the image in pieces; sha256 may be NULL
 * (the page has no WebCrypto over plain HTTP; MCUboot checks the
 * signature anyway). */
int ws_ota_web_begin(size_t total, const uint8_t *sha256);
int ws_ota_web_chunk(const uint8_t *data, size_t len);
/* err 0: all data delivered; 0 when the image is verified and marked. */
int ws_ota_web_finish(int err);
const char *ws_ota_state_str(void);
/* {"state", "progress", "version", "error", "confirmed"} for API and MQTT */
int ws_ota_status_json(char *buf, size_t len);
#else
static inline int ws_ota_request_json(const char *json, size_t len)
{
	return -95; /* -ENOTSUP */
}
static inline const char *ws_ota_state_str(void)
{
	return "disabled";
}
#endif

#endif
