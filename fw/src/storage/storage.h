/* Screen configuration files on LittleFS: current and previous version. */
#ifndef WS_STORAGE_H_
#define WS_STORAGE_H_

#include <stddef.h>

#define WS_CFG_DIR      "/lfs/cfg"
#define WS_CFG_CURRENT  WS_CFG_DIR "/screens.json"
#define WS_CFG_PREVIOUS WS_CFG_DIR "/screens.prev.json"
#define WS_CFG_TMP      WS_CFG_DIR "/screens.tmp"

/* Minimum time between two writes (flash wear, spec section 11). */
#define WS_CFG_SAVE_MIN_MS 5000

int ws_storage_init(void);
/* Reads a whole file; returns the length or a negative errno. */
int ws_storage_read(const char *path, char *buf, size_t cap);
/* Writes a new current file; the old one becomes the previous version. */
int ws_storage_save_cfg(const char *json, size_t len);
/* Swaps current and previous. */
int ws_storage_rollback(void);
/* Writes raw bytes (tests: a broken file on the flash simulator). */
int ws_storage_write_raw(const char *path, const void *data, size_t len);

/* Device settings in NVS through the settings subsystem. */
int ws_settings_init(void);

#endif
