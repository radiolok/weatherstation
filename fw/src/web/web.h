/* Web page and REST API (F8, spec section 10). */
#ifndef WS_WEB_H_
#define WS_WEB_H_

#include <stdbool.h>
#include <stddef.h>

#include <ws/json.h>

#define WS_WEB_RESP_MAX (32 * 1024 + 512)

struct api_resp {
	int status; /* HTTP status */
	char *buf;  /* response body (JSON) */
	size_t cap;
	size_t len;
};

/* Handlers: body is NUL-terminated (may be empty). */
typedef void (*api_fn)(int method, char *body, size_t len, struct api_resp *r);

void api_status(int m, char *b, size_t n, struct api_resp *r);
void api_screens(int m, char *b, size_t n, struct api_resp *r);
void api_screens_validate(int m, char *b, size_t n, struct api_resp *r);
void api_screens_rollback(int m, char *b, size_t n, struct api_resp *r);
void api_screens_factory(int m, char *b, size_t n, struct api_resp *r);
void api_catalog(int m, char *b, size_t n, struct api_resp *r);
void api_glyphs(int m, char *b, size_t n, struct api_resp *r);
void api_vars(int m, char *b, size_t n, struct api_resp *r);
void api_display_state(int m, char *b, size_t n, struct api_resp *r);
void api_render(int m, char *b, size_t n, struct api_resp *r);
void api_preview(int m, char *b, size_t n, struct api_resp *r);
void api_pin(int m, char *b, size_t n, struct api_resp *r);
void api_settings(int m, char *b, size_t n, struct api_resp *r);
void api_wifi_scan(int m, char *b, size_t n, struct api_resp *r);
void api_lamp(int m, char *b, size_t n, struct api_resp *r);

/* Helpers */
void api_error(struct api_resp *r, int status, const char *msg);
void api_json(struct api_resp *r, int status, struct ws_jw *w);

#endif
