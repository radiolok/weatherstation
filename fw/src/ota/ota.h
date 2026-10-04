/* Firmware update over the network (F10). */
#ifndef WS_OTA_H_
#define WS_OTA_H_

#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_WS_OTA
/* {"url": "http://...", "sha256": "<64 hex>", "version": "1.2.3"} from MQTT */
int ws_ota_request_json(const char *json, size_t len);
/* Upload through the web page: chunks of the image, then finish. */
int ws_ota_upload_begin(size_t total, const uint8_t sha256[32]);
int ws_ota_upload_chunk(const uint8_t *data, size_t len);
int ws_ota_upload_finish(void);
const char *ws_ota_state_str(void);
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
