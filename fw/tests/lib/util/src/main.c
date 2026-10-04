/* SHA-256 vectors (FIPS 180-2), base64 (RFC 4648), hex. */
#include <string.h>
#include <zephyr/ztest.h>

#include <ws/util.h>

static void sha_hex(const char *in, char out[65])
{
	uint8_t d[32];

	ws_sha256(in, strlen(in), d);
	ws_hex(d, 32, out);
}

ZTEST(util, test_sha256)
{
	char h[65];

	sha_hex("", h);
	zassert_str_equal(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	sha_hex("abc", h);
	zassert_str_equal(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	sha_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", h);
	zassert_str_equal(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	/* streaming in odd pieces */
	struct ws_sha256 s;
	uint8_t d[32];
	static uint8_t mil[1000];

	memset(mil, 'a', sizeof(mil));
	ws_sha256_init(&s);
	for (int i = 0; i < 1000; i++) {
		ws_sha256_update(&s, mil, 1000);
	}
	ws_sha256_final(&s, d);
	ws_hex(d, 32, h);
	zassert_str_equal(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

ZTEST(util, test_base64_hex)
{
	const char *in[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
	const char *out[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
	char b[16];
	uint8_t d[16];

	for (int i = 0; i < 7; i++) {
		zassert_equal(ws_base64_encode((const uint8_t *)in[i], strlen(in[i]), b, sizeof(b)),
			      (int)strlen(out[i]));
		zassert_str_equal(b, out[i]);
		int n = ws_base64_decode(out[i], strlen(out[i]), d, sizeof(d));

		zassert_equal(n, (int)strlen(in[i]));
		zassert_mem_equal(d, in[i], n);
	}
	zassert_equal(ws_base64_decode("Zm9v!", 5, d, sizeof(d)), -1);
	zassert_equal(ws_base64_encode((const uint8_t *)"foobar", 6, b, 8), -1);
	zassert_ok(ws_unhex("00ff10", d, 3));
	zassert_equal(d[1], 0xff);
	zassert_equal(ws_unhex("0g", d, 1), -1);
	zassert_true(ws_ct_equal("abc", "abc", 3));
	zassert_false(ws_ct_equal("abc", "abd", 3));
}

ZTEST(util, test_url)
{
	struct ws_url u;

	zassert_ok(ws_url_parse("https://tgftp.nws.noaa.gov/data/UNNT.TXT", &u));
	zassert_true(u.tls);
	zassert_equal(u.port, 443);
	zassert_str_equal(u.host, "tgftp.nws.noaa.gov");
	zassert_str_equal(u.path, "/data/UNNT.TXT");
	zassert_ok(ws_url_parse("http://192.168.1.5:8000?a=1#frag", &u));
	zassert_false(u.tls);
	zassert_equal(u.port, 8000);
	zassert_str_equal(u.host, "192.168.1.5");
	zassert_str_equal(u.path, "/?a=1");
	zassert_ok(ws_url_parse("https://h", &u));
	zassert_str_equal(u.path, "/");
	zassert_equal(ws_url_parse("ftp://h/x", &u), -1);
	zassert_equal(ws_url_parse("https://:80/x", &u), -1);
	zassert_equal(ws_url_parse("https://h:99999/x", &u), -1);
	zassert_equal(ws_url_parse("https://h:x/", &u), -1);
}

ZTEST_SUITE(util, NULL, NULL, NULL, NULL, NULL);
