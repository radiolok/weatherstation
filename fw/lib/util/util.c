/* SHA-256 (FIPS 180-4), base64 and hex, see ws/util.h. */
#include <string.h>

#include <ws/util.h>

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
	0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
	0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
	0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
	0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
	0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
	0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void block(struct ws_sha256 *s, const uint8_t *p)
{
	uint32_t w[64], a, b, c, d, e, f, g, h;

	for (int i = 0; i < 16; i++) {
		w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
		       (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
	}
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);

		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = s->h[0];
	b = s->h[1];
	c = s->h[2];
	d = s->h[3];
	e = s->h[4];
	f = s->h[5];
	g = s->h[6];
	h = s->h[7];
	for (int i = 0; i < 64; i++) {
		uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) +
			      K[i] + w[i];
		uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));

		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	s->h[0] += a;
	s->h[1] += b;
	s->h[2] += c;
	s->h[3] += d;
	s->h[4] += e;
	s->h[5] += f;
	s->h[6] += g;
	s->h[7] += h;
}

void ws_sha256_init(struct ws_sha256 *s)
{
	static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
				       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

	memcpy(s->h, iv, sizeof(iv));
	s->len = 0;
	s->n = 0;
}

void ws_sha256_update(struct ws_sha256 *s, const void *data, size_t len)
{
	const uint8_t *p = data;

	s->len += len;
	while (len) {
		size_t k = 64 - s->n < len ? 64 - s->n : len;

		memcpy(s->buf + s->n, p, k);
		s->n += k;
		p += k;
		len -= k;
		if (s->n == 64) {
			block(s, s->buf);
			s->n = 0;
		}
	}
}

void ws_sha256_final(struct ws_sha256 *s, uint8_t out[32])
{
	uint64_t bits = s->len * 8;
	uint8_t pad = 0x80;
	uint8_t z = 0;

	ws_sha256_update(s, &pad, 1);
	while (s->n != 56) {
		ws_sha256_update(s, &z, 1);
	}
	for (int i = 7; i >= 0; i--) {
		uint8_t b = (uint8_t)(bits >> (i * 8));

		ws_sha256_update(s, &b, 1);
	}
	for (int i = 0; i < 8; i++) {
		out[i * 4] = (uint8_t)(s->h[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
		out[i * 4 + 3] = (uint8_t)s->h[i];
	}
}

void ws_sha256(const void *data, size_t len, uint8_t out[32])
{
	struct ws_sha256 s;

	ws_sha256_init(&s);
	ws_sha256_update(&s, data, len);
	ws_sha256_final(&s, out);
}

void ws_hex(const uint8_t *in, size_t n, char *out)
{
	static const char d[] = "0123456789abcdef";

	for (size_t i = 0; i < n; i++) {
		out[i * 2] = d[in[i] >> 4];
		out[i * 2 + 1] = d[in[i] & 15];
	}
	out[n * 2] = '\0';
}

static int hv(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

int ws_unhex(const char *in, uint8_t *out, size_t n)
{
	if (strlen(in) != n * 2) {
		return -1;
	}
	for (size_t i = 0; i < n; i++) {
		int a = hv(in[i * 2]), b = hv(in[i * 2 + 1]);

		if (a < 0 || b < 0) {
			return -1;
		}
		out[i] = (uint8_t)(a << 4 | b);
	}
	return 0;
}

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int ws_base64_encode(const uint8_t *in, size_t n, char *out, size_t cap)
{
	size_t o = 0;

	if ((n + 2) / 3 * 4 + 1 > cap) {
		return -1;
	}
	for (size_t i = 0; i < n; i += 3) {
		uint32_t v = (uint32_t)in[i] << 16;

		if (i + 1 < n) {
			v |= (uint32_t)in[i + 1] << 8;
		}
		if (i + 2 < n) {
			v |= in[i + 2];
		}
		out[o++] = b64[v >> 18];
		out[o++] = b64[(v >> 12) & 63];
		out[o++] = i + 1 < n ? b64[(v >> 6) & 63] : '=';
		out[o++] = i + 2 < n ? b64[v & 63] : '=';
	}
	out[o] = '\0';
	return (int)o;
}

int ws_base64_decode(const char *in, size_t n, uint8_t *out, size_t cap)
{
	uint32_t v = 0;
	int bits = 0;
	size_t o = 0;

	for (size_t i = 0; i < n; i++) {
		char c = in[i];
		const char *p;

		if (c == '=') {
			break;
		}
		if (c == '\r' || c == '\n' || c == ' ') {
			continue;
		}
		p = strchr(b64, c);
		if (!p || !c) {
			return -1;
		}
		v = v << 6 | (uint32_t)(p - b64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			if (o >= cap) {
				return -1;
			}
			out[o++] = (uint8_t)(v >> bits);
		}
	}
	return (int)o;
}

bool ws_ct_equal(const void *a, const void *b, size_t n)
{
	const uint8_t *x = a, *y = b;
	uint8_t d = 0;

	for (size_t i = 0; i < n; i++) {
		d |= x[i] ^ y[i];
	}
	return d == 0;
}
