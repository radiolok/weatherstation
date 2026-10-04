/*
 * Blocking HTTP(S) GET with a streaming body callback. Shared by the METAR
 * fallback and the OTA download.
 */
#ifndef WS_HTTP_GET_H_
#define WS_HTTP_GET_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/net/tls_credentials.h>

/* Body data; return 0 to go on, negative to abort the transfer. */
typedef int (*ws_http_body_cb)(const uint8_t *data, size_t len, size_t total, void *user);

struct ws_http_get {
	const char *url;
	const sec_tag_t *tags; /* TLS: CA credentials to verify the server */
	size_t n_tags;
	int32_t timeout_ms;
	ws_http_body_cb body;
	void *user;
	/* results */
	uint16_t status;       /* HTTP status code, 0 if none */
	size_t content_length; /* 0 if not sent */
	size_t received;
};

/* Returns 0 when the response was 200 and the body was delivered, else a
 * negative errno (-EPROTO for another status, -ECONNABORTED if the body
 * callback aborted). */
int ws_http_get(struct ws_http_get *req);

#endif
