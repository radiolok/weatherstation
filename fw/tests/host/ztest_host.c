/* Runner for the host ztest shim, see include/zephyr/ztest.h. */
#include <stdarg.h>
#include <zephyr/ztest.h>

static struct ztest_host_test *tests;
static struct ztest_host_suite *suites;
static jmp_buf jump;
static int skipped;

void ztest_host_register_test(struct ztest_host_test *t)
{
	/* Keep declaration order: append at the tail. */
	struct ztest_host_test **p = &tests;

	while (*p) {
		p = &(*p)->next;
	}
	*p = t;
}

void ztest_host_register_suite(struct ztest_host_suite *s)
{
	s->next = suites;
	suites = s;
}

void ztest_host_fail(const char *file, int line, const char *expr, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "  ASSERT %s:%d: %s", file, line, expr);
	if (fmt && fmt[0] && fmt[1]) {
		fprintf(stderr, ": ");
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
	fprintf(stderr, "\n");
	longjmp(jump, 1);
}

void ztest_host_skip(void)
{
	skipped = 1;
	longjmp(jump, 2);
}

static int run_one(struct ztest_host_suite *s, struct ztest_host_test *t, void *fixture)
{
	int rc = setjmp(jump);

	if (rc == 0) {
		if (s->before) {
			s->before(fixture);
		}
		t->fn(fixture);
	}
	if (s->after) {
		s->after(fixture);
	}
	return rc;
}

int main(int argc, char **argv)
{
	int run = 0, failed = 0, skip = 0;
	const char *filter = argc > 1 ? argv[1] : NULL;

	for (struct ztest_host_suite *s = suites; s; s = s->next) {
		void *fixture = NULL;

		if (s->predicate && !s->predicate(NULL)) {
			continue;
		}
		if (s->setup) {
			fixture = s->setup();
		}
		printf("Running TESTSUITE %s\n", s->name);
		for (struct ztest_host_test *t = tests; t; t = t->next) {
			if (strcmp(t->suite, s->name) != 0) {
				continue;
			}
			if (filter && !strstr(t->name, filter)) {
				continue;
			}
			run++;
			skipped = 0;
			int rc = run_one(s, t, fixture);

			if (rc == 1) {
				failed++;
				printf(" FAIL - %s\n", t->name);
			} else if (rc == 2) {
				skip++;
				printf(" SKIP - %s\n", t->name);
			} else {
				printf(" PASS - %s\n", t->name);
			}
		}
		if (s->teardown) {
			s->teardown(fixture);
		}
	}
	printf("%s: %d run, %d failed, %d skipped\n", failed ? "FAILED" : "PASSED", run, failed,
	       skip);
	return failed ? 1 : 0;
}
