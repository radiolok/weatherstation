/* Small self-contained helpers: SHA-256, base64, hex, URLs. */
#ifndef WS_UTIL_H_
#define WS_UTIL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ws_sha256 {
	uint32_t h[8];
	uint64_t len;
	uint8_t buf[64];
	size_t n;
};

void ws_sha256_init(struct ws_sha256 *s);
void ws_sha256_update(struct ws_sha256 *s, const void *data, size_t len);
void ws_sha256_final(struct ws_sha256 *s, uint8_t out[32]);
void ws_sha256(const void *data, size_t len, uint8_t out[32]);

void ws_hex(const uint8_t *in, size_t n, char *out); /* 2n + 1 bytes */
/* Parses exactly n bytes of hex (case-insensitive), 0 or -1. */
int ws_unhex(const char *in, uint8_t *out, size_t n);

/* Standard base64 with padding. Returns the output length or -1. */
int ws_base64_encode(const uint8_t *in, size_t n, char *out, size_t cap);
int ws_base64_decode(const char *in, size_t n, uint8_t *out, size_t cap);

/* Constant-time comparison for secrets. */
bool ws_ct_equal(const void *a, const void *b, size_t n);

struct ws_url {
	bool tls;      /* https */
	uint16_t port; /* explicit or 80/443 */
	char host[64];
	char path[192]; /* with the query, "/" when empty */
};

/* Parses "http[s]://host[:port][/path?query]". 0 or -1. */
int ws_url_parse(const char *url, struct ws_url *u);

#ifdef __cplusplus
}
#endif

#endif
