/* LittleFS storage of the screen configuration (spec, section 8). */
#include <errno.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "storage.h"

LOG_MODULE_REGISTER(ws_storage, LOG_LEVEL_INF);

static int64_t last_save_ms = -WS_CFG_SAVE_MIN_MS;
K_MUTEX_DEFINE(fs_lock);

int ws_storage_init(void)
{
	/* /lfs is mounted by the fstab node (automount) */
	int r = fs_mkdir(WS_CFG_DIR);

	if (r < 0 && r != -EEXIST) {
		LOG_ERR("mkdir %s: %d", WS_CFG_DIR, r);
		return r;
	}
	return 0;
}

int ws_storage_read(const char *path, char *buf, size_t cap)
{
	struct fs_file_t f;
	ssize_t n, total = 0;
	int r;

	fs_file_t_init(&f);
	k_mutex_lock(&fs_lock, K_FOREVER);
	r = fs_open(&f, path, FS_O_READ);
	if (r < 0) {
		k_mutex_unlock(&fs_lock);
		return r;
	}
	while ((size_t)total < cap) {
		n = fs_read(&f, buf + total, cap - total);
		if (n <= 0) {
			break;
		}
		total += n;
	}
	fs_close(&f);
	k_mutex_unlock(&fs_lock);
	if ((size_t)total >= cap) {
		return -EFBIG;
	}
	buf[total] = '\0';
	return (int)total;
}

static int write_file(const char *path, const void *data, size_t len)
{
	struct fs_file_t f;
	int r;

	fs_file_t_init(&f);
	fs_unlink(path);
	r = fs_open(&f, path, FS_O_CREATE | FS_O_WRITE);
	if (r < 0) {
		return r;
	}
	ssize_t n = fs_write(&f, data, len);

	if (n >= 0 && (size_t)n != len) {
		n = -ENOSPC;
	}
	r = fs_sync(&f);
	fs_close(&f);
	return n < 0 ? (int)n : r;
}

int ws_storage_write_raw(const char *path, const void *data, size_t len)
{
	k_mutex_lock(&fs_lock, K_FOREVER);
	int r = write_file(path, data, len);

	k_mutex_unlock(&fs_lock);
	return r;
}

int ws_storage_save_cfg(const char *json, size_t len)
{
	struct fs_dirent st;
	int r;

	/* no more often than once per 5 s */
	int64_t wait = last_save_ms + WS_CFG_SAVE_MIN_MS - k_uptime_get();

	if (wait > 0) {
		k_msleep((int32_t)wait);
	}
	k_mutex_lock(&fs_lock, K_FOREVER);
	r = write_file(WS_CFG_TMP, json, len);
	if (r == 0 && fs_stat(WS_CFG_CURRENT, &st) == 0) {
		r = fs_rename(WS_CFG_CURRENT, WS_CFG_PREVIOUS);
	}
	if (r == 0) {
		r = fs_rename(WS_CFG_TMP, WS_CFG_CURRENT);
	}
	last_save_ms = k_uptime_get();
	k_mutex_unlock(&fs_lock);
	if (r < 0) {
		LOG_ERR("saving screens failed: %d", r);
	}
	return r;
}

int ws_storage_rollback(void)
{
	struct fs_dirent st;
	int r;

	k_mutex_lock(&fs_lock, K_FOREVER);
	r = fs_stat(WS_CFG_PREVIOUS, &st);
	if (r == 0) {
		if (fs_stat(WS_CFG_CURRENT, &st) == 0) {
			r = fs_rename(WS_CFG_CURRENT, WS_CFG_TMP);
		}
		if (r == 0) {
			r = fs_rename(WS_CFG_PREVIOUS, WS_CFG_CURRENT);
		}
		if (r == 0 && fs_stat(WS_CFG_TMP, &st) == 0) {
			r = fs_rename(WS_CFG_TMP, WS_CFG_PREVIOUS);
		}
	}
	k_mutex_unlock(&fs_lock);
	return r;
}
