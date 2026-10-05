/*
 * Minimal host implementation of the ztest API subset used by fw/tests/lib.
 *
 * The same test sources build as real ztest applications on native_sim
 * (twister, CI) and as plain host executables with this header
 * (fw/scripts/host-tests.sh) for a fast edit-test loop without Zephyr.
 */
#ifndef WS_HOST_ZTEST_H_
#define WS_HOST_ZTEST_H_

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ztest_host_test {
	const char *suite;
	const char *name;
	void (*fn)(void *fixture);
	struct ztest_host_test *next;
};

struct ztest_host_suite {
	const char *name;
	bool (*predicate)(const void *state);
	void *(*setup)(void);
	void (*before)(void *fixture);
	void (*after)(void *fixture);
	void (*teardown)(void *fixture);
	struct ztest_host_suite *next;
};

void ztest_host_register_test(struct ztest_host_test *t);
void ztest_host_register_suite(struct ztest_host_suite *s);
void ztest_host_fail(const char *file, int line, const char *expr, const char *fmt, ...)
	__attribute__((noreturn, format(printf, 4, 5)));
void ztest_host_skip(void) __attribute__((noreturn));

#define ZTEST_SUITE(_suite, _pred, _setup, _before, _after, _teardown)                             \
	static struct ztest_host_suite _ztest_suite_##_suite = {                                   \
		#_suite, _pred, _setup, _before, _after, _teardown, NULL};                         \
	__attribute__((constructor)) static void _ztest_reg_suite_##_suite(void)                   \
	{                                                                                          \
		ztest_host_register_suite(&_ztest_suite_##_suite);                                 \
	}

#define ZTEST(_suite, _name)                                                                       \
	static void _ztest_fn_##_suite##_##_name(void);                                            \
	static void _ztest_wrap_##_suite##_##_name(void *f)                                        \
	{                                                                                          \
		(void)f;                                                                           \
		_ztest_fn_##_suite##_##_name();                                                    \
	}                                                                                          \
	static struct ztest_host_test _ztest_t_##_suite##_##_name = {                              \
		#_suite, #_name, _ztest_wrap_##_suite##_##_name, NULL};                            \
	__attribute__((constructor)) static void _ztest_reg_##_suite##_##_name(void)               \
	{                                                                                          \
		ztest_host_register_test(&_ztest_t_##_suite##_##_name);                            \
	}                                                                                          \
	static void _ztest_fn_##_suite##_##_name(void)

#define ZTEST_F(_suite, _name)                                                                     \
	static void _ztest_fn_##_suite##_##_name(struct _suite##_fixture *fixture);                \
	static void _ztest_wrap_##_suite##_##_name(void *f)                                        \
	{                                                                                          \
		_ztest_fn_##_suite##_##_name((struct _suite##_fixture *)f);                        \
	}                                                                                          \
	static struct ztest_host_test _ztest_t_##_suite##_##_name = {                              \
		#_suite, #_name, _ztest_wrap_##_suite##_##_name, NULL};                            \
	__attribute__((constructor)) static void _ztest_reg_##_suite##_##_name(void)               \
	{                                                                                          \
		ztest_host_register_test(&_ztest_t_##_suite##_##_name);                            \
	}                                                                                          \
	static void _ztest_fn_##_suite##_##_name(struct _suite##_fixture *fixture)

#define ZT_MSG(...) " " __VA_ARGS__

#define zassert_true(cond, ...)                                                                    \
	do {                                                                                       \
		if (!(cond)) {                                                                     \
			ztest_host_fail(__FILE__, __LINE__, #cond " is false",                     \
					ZT_MSG(__VA_ARGS__));                                      \
		}                                                                                  \
	} while (0)
#define zassert_false(cond, ...)                                                                   \
	do {                                                                                       \
		if (cond) {                                                                        \
			ztest_host_fail(__FILE__, __LINE__, #cond " is true",                      \
					ZT_MSG(__VA_ARGS__));                                      \
		}                                                                                  \
	} while (0)
#define zassert_equal(a, b, ...)                                                                   \
	do {                                                                                       \
		long long _za = (long long)(a), _zb = (long long)(b);                              \
		if (_za != _zb) {                                                                  \
			fprintf(stderr, "  %s = %lld, %s = %lld\n", #a, _za, #b, _zb);             \
			ztest_host_fail(__FILE__, __LINE__, #a " != " #b, ZT_MSG(__VA_ARGS__));    \
		}                                                                                  \
	} while (0)
#define zassert_not_equal(a, b, ...)                                                               \
	do {                                                                                       \
		if ((a) == (b)) {                                                                  \
			ztest_host_fail(__FILE__, __LINE__, #a " == " #b, ZT_MSG(__VA_ARGS__));    \
		}                                                                                  \
	} while (0)
#define zassert_equal_ptr(a, b, ...)                                                               \
	zassert_true((const void *)(a) == (const void *)(b), __VA_ARGS__)
#define zassert_is_null(p, ...)  zassert_true((p) == NULL, __VA_ARGS__)
#define zassert_not_null(p, ...) zassert_true((p) != NULL, __VA_ARGS__)
#define zassert_ok(x, ...)       zassert_equal((x), 0, __VA_ARGS__)
#define zassert_str_equal(a, b, ...)                                                               \
	do {                                                                                       \
		const char *_sa = (a), *_sb = (b);                                                 \
		if (strcmp(_sa, _sb) != 0) {                                                       \
			fprintf(stderr, "  %s = \"%s\"\n  %s = \"%s\"\n", #a, _sa, #b, _sb);       \
			ztest_host_fail(__FILE__, __LINE__, #a " != " #b, ZT_MSG(__VA_ARGS__));    \
		}                                                                                  \
	} while (0)
#define zassert_unreachable(...)                                                                   \
	ztest_host_fail(__FILE__, __LINE__, "unreachable", ZT_MSG(__VA_ARGS__))
#define zassert_within(a, b, d, ...)                                                               \
	do {                                                                                       \
		double _wa = (double)(a), _wb = (double)(b), _wd = (double)(d);                    \
		if (_wa < _wb - _wd || _wa > _wb + _wd) {                                          \
			fprintf(stderr, "  %s = %g, %s = %g\n", #a, _wa, #b, _wb);                 \
			ztest_host_fail(__FILE__, __LINE__, #a " not within " #d " of " #b,        \
					ZT_MSG(__VA_ARGS__));                                      \
		}                                                                                  \
	} while (0)
#define zassert_between_inclusive(a, lo, hi, ...)                                                  \
	zassert_true((a) >= (lo) && (a) <= (hi), __VA_ARGS__)
#define zassert_mem_equal(a, b, n, ...) zassert_true(memcmp((a), (b), (n)) == 0, __VA_ARGS__)
#define zassert_str_equal(a, b, ...)                                                               \
	do {                                                                                       \
		const char *_sa = (a), *_sb = (b);                                                 \
		if (strcmp(_sa, _sb) != 0) {                                                       \
			fprintf(stderr, "  %s = \"%s\"\n  %s = \"%s\"\n", #a, _sa, #b, _sb);       \
			ztest_host_fail(__FILE__, __LINE__, #a " != " #b, ZT_MSG(__VA_ARGS__));    \
		}                                                                                  \
	} while (0)
#define ztest_test_skip() ztest_host_skip()

#define TC_PRINT(...) printf(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* WS_HOST_ZTEST_H_ */
