/* METAR fallback service (fw/src/metar/metar.c). */
#ifndef WS_METAR_SVC_H_
#define WS_METAR_SVC_H_

#include <stdint.h>

#include <ws/metar_map.h>

struct ws_metar_status {
	char report[WS_METAR_MAX_LEN]; /* last accepted report */
	int64_t last_ok;               /* unix time of the last good fetch, 0 never */
	int64_t obs_unix;              /* observation time of that report */
	int source;                    /* 0 primary URL, 1 fallback URL */
	int last_err;                  /* 0 or a negative errno of the last attempt */
	uint32_t attempts;
};

int ws_metar_fetch_now(void);
/* Decodes a raw report and updates obs.* (shell, tests). */
int ws_metar_ingest(const char *raw);
void ws_metar_status_get(struct ws_metar_status *out);

#endif
