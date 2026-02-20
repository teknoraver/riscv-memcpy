#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>

typedef void *(memcpy_t)(void *, const void *, size_t);

extern memcpy_t memcpy_64x10, memcpy_v;

static struct memcpy_func {
	const char *name;
	memcpy_t *func;
} memcpys[] = {
	{ "libc memcpy", memcpy },
	{ "memcpy_64x10", memcpy_64x10 },
	{ "memcpy_v", memcpy_v },
};

static sigjmp_buf jmp_env;

static void sigill_handler(int sig) {
	siglongjmp(jmp_env, 1);
}

static int test(memcpy_t func, char *buf1, char *buf2, size_t size)
{
	for (int i = 0; i < size; i++)
		buf1[i] = random();

	func(buf2, buf1, size);

	if (memcmp(buf1, buf2, size)) {
		printf("!differ, size: %lu, alignment: %ld\n", size, (buf1 - buf2) & 7);
		return 1;
	}

	return 0;
}

static int tests(memcpy_t func)
{
	char buf1[256], buf2[256];
	int ret = 0;

	srandom(time(NULL));

	ret |= test(func, buf1, buf2, 0);

	ret |= test(func, buf1, buf2, sizeof(buf1));
	ret |= test(func, buf1, buf2, 16);
	ret |= test(func, buf1, buf2, 3);

	ret |= test(func, buf1+1, buf2, 200);
	ret |= test(func, buf1+1, buf2+1, 200);
	ret |= test(func, buf1, buf2+1, 200);
	ret |= test(func, buf1, buf2+5, 200);
	ret |= test(func, buf1+2, buf2+4, 200);

	ret |= test(func, buf1, buf2+1, 40);
	ret |= test(func, buf1+3, buf2+1, 40);

	return ret;
}

#define BENCH_SIZE (128 * 1024 * 1024)
#define LOOPS 10

int bench(memcpy_t func, int offset)
{
	char *src = malloc(BENCH_SIZE);
	char *dst = malloc(BENCH_SIZE);
	struct timespec start, end;
	uint64_t total_ns = 0;

	for (int i = 0; i < BENCH_SIZE; i++)
		src[i] = dst[i] = i;

	for (int i = 0; i < LOOPS; i++) {
		clock_gettime(CLOCK_MONOTONIC, &start);
		func(dst + offset, src, BENCH_SIZE - offset);
		clock_gettime(CLOCK_MONOTONIC, &end);

		// Prevent the compiler from optimizing away the memcpy.
		asm volatile("" :: "m"(*(char(*)[BENCH_SIZE])dst));
		total_ns += (end.tv_sec - start.tv_sec) * 1000000000L
			  + (end.tv_nsec - start.tv_nsec);
	}

	free(src);
	free(dst);

	double secs = total_ns / 1e9;
	double mb = (double)BENCH_SIZE * LOOPS / (1024.0 * 1024.0);
	printf("  %saligned: %.2f MB/s\n", offset ? "un" : "", mb / secs);

	return 0;
}

int test_func(memcpy_t func)
{
	if (tests(func))
		return 1;

	bench(func, 0);
	bench(func, 1);

	return 0;
}

int main(int argc, char *argv[])
{
	signal(SIGILL, sigill_handler);

	for (size_t i = 0; i < sizeof(memcpys) / sizeof(memcpys[0]); i++) {
		printf("Testing %s...\n", memcpys[i].name);
		if (sigsetjmp(jmp_env, 1)) {
			printf("%s: SIGILL, skipping\n", memcpys[i].name);
			continue;
		}
		if (test_func(memcpys[i].func))
			return 1;
	}

	return 0;
}
