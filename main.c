#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "spec_freq.h"

typedef void *(memcpy_t)(void *, const void *, size_t);

extern memcpy_t memcpy_64x10, memcpy_csky, memcpy_shifted, memcpy_shifted_bl2,
	memcpy_shifted_bits, memcpy_shifted_jt, memcpy_v;

static struct memcpy_func {
	const char *name;
	const char *tag;	/* column header in the size table */
	memcpy_t *func;
	int skip;		/* SIGILL or failed tests */
} memcpys[] = {
	{ "libc memcpy", "libc", memcpy },
	{ "memcpy using 10+6 registers", "64x10", memcpy_64x10 },
	{ "memcpy by csky", "csky", memcpy_csky },
	{ "memcpy shifted (glibc patch)", "shifted", memcpy_shifted },
	{ "memcpy shifted, 2-byte loop below 16", "bl2", memcpy_shifted_bl2 },
	{ "memcpy shifted, 8/4/2/1 blocks below 16", "bits", memcpy_shifted_bits },
	{ "memcpy shifted, jump table below 16", "jt", memcpy_shifted_jt },
	{ "memcpy vector", "vector", memcpy_v },
};

#define NFUNCS (sizeof(memcpys) / sizeof(memcpys[0]))

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

/*
 * Every length up to EX_MAXLEN (plus a few larger ones) with every
 * source and destination alignment.  Checks the copied bytes, that the
 * bytes around the destination are untouched and the return value.
 */
#define EX_MAXLEN	300
#define EX_GUARD	32

static int test_exhaustive(memcpy_t func)
{
	static const size_t big[] = { 511, 512, 513, 1000, 4095, 4096, 4103 };
	static unsigned char src[4200 + 2 * EX_GUARD], dst[4200 + 2 * EX_GUARD];
	size_t nlen = EX_MAXLEN + 1 + sizeof(big) / sizeof(big[0]);

	for (size_t i = 0; i < sizeof(src); i++)
		src[i] = random();

	for (size_t l = 0; l < nlen; l++) {
		size_t len = l <= EX_MAXLEN ? l : big[l - EX_MAXLEN - 1];

		for (int sa = 0; sa < 8; sa++) {
			for (int da = 0; da < 8; da++) {
				unsigned char *s = src + EX_GUARD + sa;
				unsigned char *d = dst + EX_GUARD + da;

				memset(dst, 0xa5, sizeof(dst));
				if (func(d, s, len) != d) {
					printf("!bad return value, len %zu, src+%d, dst+%d\n", len, sa, da);
					return 1;
				}
				if (memcmp(d, s, len)) {
					printf("!differ, len %zu, src+%d, dst+%d\n", len, sa, da);
					return 1;
				}
				for (unsigned char *p = dst; p < dst + sizeof(dst); p++) {
					if ((p < d || p >= d + len) && *p != 0xa5) {
						printf("!write outside dst at %td, len %zu, src+%d, dst+%d\n",
						       p - d, len, sa, da);
						return 1;
					}
				}
			}
		}
	}

	return 0;
}

/*
 * Put the source right after and right before an inaccessible page:
 * a read before SRC or after SRC + LEN - 1 that crosses into another
 * page crashes.  Done in a child so that a crash is reported, not fatal.
 */
static int test_overread(memcpy_t func)
{
	long pg = sysconf(_SC_PAGESIZE);
	unsigned char *map, *dst;
	int ret = 0;

	map = mmap(NULL, 3 * pg, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	dst = malloc(pg);
	if (map == MAP_FAILED || !dst) {
		perror("mmap");
		return 1;
	}
	for (long i = 0; i < pg; i++)
		map[pg + i] = random();
	mprotect(map, pg, PROT_NONE);
	mprotect(map + 2 * pg, pg, PROT_NONE);

	fflush(stdout);
	pid_t pid = fork();
	if (!pid) {
		for (size_t len = 0; len <= 256; len++) {
			for (int da = 0; da < 8; da++) {
				/* SRC ends at the end of the page.  */
				unsigned char *s = map + 2 * pg - len;

				func(dst + da, s, len);
				if (memcmp(dst + da, s, len))
					_exit(2);
				/* SRC starts at the start of the page.  */
				s = map + pg;
				func(dst + da, s, len);
				if (memcmp(dst + da, s, len))
					_exit(2);
			}
		}
		_exit(0);
	}

	int status;
	waitpid(pid, &status, 0);
	if (WIFSIGNALED(status)) {
		printf("!read outside src: signal %d\n", WTERMSIG(status));
		ret = 1;
	} else if (WEXITSTATUS(status)) {
		printf("!differ near a page boundary\n");
		ret = 1;
	}

	munmap(map, 3 * pg);
	free(dst);

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

/*
 * Time per call for sizes that fit in the cache, with SRC and DEST both
 * aligned and with DEST one byte off (like the large benchmark above).
 * Each value is the median of SAMPLES timings of many calls.
 */
#define SAMPLES		7
#define MAX_SIZE	(64 * 1024)

static const size_t sizes[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 24, 32, 48,
	64, 96, 128, 192, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768,
	65536,
};

#define NSIZES (sizeof(sizes) / sizeof(sizes[0]))

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return (x > y) - (x < y);
}

static double ns_per_call(memcpy_t *func, char *dst, const char *src, size_t len)
{
	/* Stop the compiler from treating FUNC as a known function.  */
	memcpy_t *volatile vf = func;
	memcpy_t *f = vf;
	long iters = len <= 256 ? 100000 : 16L * 1024 * 1024 / len;
	double t[SAMPLES];

	for (long i = 0; i < iters / 10; i++)	/* warm up */
		f(dst, src, len);

	for (int s = 0; s < SAMPLES; s++) {
		struct timespec start, end;

		clock_gettime(CLOCK_MONOTONIC, &start);
		for (long i = 0; i < iters; i++)
			f(dst, src, len);
		clock_gettime(CLOCK_MONOTONIC, &end);
		t[s] = ((end.tv_sec - start.tv_sec) * 1e9 +
			(end.tv_nsec - start.tv_nsec)) / iters;
	}
	qsort(t, SAMPLES, sizeof(t[0]), cmp_double);

	return t[SAMPLES / 2];
}

static void bench_sizes(void)
{
	char *src = aligned_alloc(64, MAX_SIZE + 64);
	char *dst = aligned_alloc(64, MAX_SIZE + 64);
	static double res[2][NSIZES][NFUNCS];

	memset(src, 1, MAX_SIZE + 64);
	memset(dst, 2, MAX_SIZE + 64);

	for (int u = 0; u < 2; u++)
		for (size_t s = 0; s < NSIZES; s++)
			for (size_t f = 0; f < NFUNCS; f++)
				if (!memcpys[f].skip)
					res[u][s][f] = ns_per_call(memcpys[f].func,
								   dst + u, src, sizes[s]);

	for (int u = 0; u < 2; u++) {
		printf("\n%s, ns per call (time relative to %s)\n\n%8s",
		       u ? "DEST + 1, SRC aligned" : "SRC and DEST aligned",
		       memcpys[0].tag, "size");
		for (size_t f = 0; f < NFUNCS; f++)
			if (!memcpys[f].skip)
				printf("  %15s", memcpys[f].tag);
		printf("\n");
		for (size_t s = 0; s < NSIZES; s++) {
			printf("%8zu", sizes[s]);
			for (size_t f = 0; f < NFUNCS; f++) {
				if (memcpys[f].skip)
					continue;
				if (f)
					printf("  %8.1f (%4.2f)", res[u][s][f],
					       res[u][s][f] / res[u][s][0]);
				else
					printf("  %15.1f", res[u][s][f]);
			}
			printf("\n");
		}
	}

	free(src);
	free(dst);
}

/*
 * Calls with random lengths in [0, RANGE) and random SRC and DEST
 * alignments, so that branches on the length are not predictable.
 */
#define NRAND		4096

static void bench_random(void)
{
	static const size_t ranges[] = { 8, 16, 32, 64, 256, 1024 };
	static unsigned short lens[NRAND];
	static unsigned char soff[NRAND], doff[NRAND];
	char *src = aligned_alloc(64, 2048), *dst = aligned_alloc(64, 2048);

	memset(src, 1, 2048);
	memset(dst, 2, 2048);

	printf("\nrandom length in [0, range), random alignments, ns per call (time relative to %s)\n\n%8s",
	       memcpys[0].tag, "range");
	for (size_t f = 0; f < NFUNCS; f++)
		if (!memcpys[f].skip)
			printf("  %15s", memcpys[f].tag);
	printf("\n");

	for (size_t r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++) {
		long passes = ranges[r] <= 64 ? 50 : 4096 / ranges[r];
		double base = 0;

		srandom(r + 1);
		for (int i = 0; i < NRAND; i++) {
			lens[i] = random() % ranges[r];
			soff[i] = random() % 8;
			doff[i] = random() % 8;
		}

		printf("%8zu", ranges[r]);
		for (size_t f = 0; f < NFUNCS; f++) {
			memcpy_t *volatile vf = memcpys[f].func;
			memcpy_t *fn = vf;
			double t[SAMPLES];

			if (memcpys[f].skip)
				continue;
			for (int i = 0; i < NRAND; i++)	/* warm up */
				fn(dst + doff[i], src + soff[i], lens[i]);
			for (int s = 0; s < SAMPLES; s++) {
				struct timespec start, end;

				clock_gettime(CLOCK_MONOTONIC, &start);
				for (long p = 0; p < passes; p++)
					for (int i = 0; i < NRAND; i++)
						fn(dst + doff[i], src + soff[i], lens[i]);
				clock_gettime(CLOCK_MONOTONIC, &end);
				t[s] = ((end.tv_sec - start.tv_sec) * 1e9 +
					(end.tv_nsec - start.tv_nsec)) / (passes * NRAND);
			}
			qsort(t, SAMPLES, sizeof(t[0]), cmp_double);
			if (!base)
				base = t[SAMPLES / 2];
			if (f)
				printf("  %8.1f (%4.2f)", t[SAMPLES / 2], t[SAMPLES / 2] / base);
			else
				printf("  %15.1f", t[SAMPLES / 2]);
		}
		printf("\n");
	}

	free(src);
	free(dst);
}

/*
 * Like glibc's bench-memcpy-random: sizes and alignments follow the
 * memcpy calls in SPEC2017, SRC and DEST are random offsets in a region
 * of the given size, rounded down to the chosen alignment.
 */
#define NSPEC		8192

static void bench_spec(void)
{
	static const size_t regions[] = { 4096, 32768, 262144 };
	static uint16_t sizes_arr[65536];
	static uint8_t salign[1024], dalign[1024];
	static struct { uint32_t src, dst, len; } t[NSPEC];
	size_t n, nsz, nsa, nda;
	char *src = aligned_alloc(4096, 262144 + 8192);
	char *dst = aligned_alloc(4096, 262144 + 8192);

	memset(src, 1, 262144 + 8192);
	memset(dst, 2, 262144 + 8192);
	for (n = nsz = 0; size_freq[n].freq; n++)
		for (int j = 0; j < size_freq[n].freq; j++)
			sizes_arr[nsz++] = size_freq[n].size;
	for (n = nsa = 0; src_align_freq[n].freq; n++)
		for (int j = 0; j < src_align_freq[n].freq; j++)
			salign[nsa++] = src_align_freq[n].align - 1;
	for (n = nda = 0; dst_align_freq[n].freq; n++)
		for (int j = 0; j < dst_align_freq[n].freq; j++)
			dalign[nda++] = dst_align_freq[n].align - 1;

	printf("\nSPEC2017 sizes and alignments (bench-memcpy-random), ns per call (time relative to %s)\n\n%8s",
	       memcpys[0].tag, "region");
	for (size_t f = 0; f < NFUNCS; f++)
		if (!memcpys[f].skip)
			printf("  %15s", memcpys[f].tag);
	printf("\n");

	for (size_t r = 0; r < sizeof(regions) / sizeof(regions[0]); r++) {
		double base = 0;

		srandom(r + 1);
		for (int i = 0; i < NSPEC; i++) {
			t[i].dst = (random() % regions[r]) & ~dalign[random() % nda];
			t[i].src = (random() % regions[r]) & ~salign[random() % nsa];
			t[i].len = sizes_arr[random() % nsz];
		}

		printf("%8zu", regions[r]);
		for (size_t f = 0; f < NFUNCS; f++) {
			memcpy_t *volatile vf = memcpys[f].func;
			memcpy_t *fn = vf;
			double tm[SAMPLES];

			if (memcpys[f].skip)
				continue;
			for (int i = 0; i < NSPEC; i++)	/* warm up */
				fn(dst + t[i].dst, src + t[i].src, t[i].len);
			for (int s = 0; s < SAMPLES; s++) {
				struct timespec start, end;

				clock_gettime(CLOCK_MONOTONIC, &start);
				for (int p = 0; p < 20; p++)
					for (int i = 0; i < NSPEC; i++)
						fn(dst + t[i].dst, src + t[i].src, t[i].len);
				clock_gettime(CLOCK_MONOTONIC, &end);
				tm[s] = ((end.tv_sec - start.tv_sec) * 1e9 +
					 (end.tv_nsec - start.tv_nsec)) / (20 * NSPEC);
			}
			qsort(tm, SAMPLES, sizeof(tm[0]), cmp_double);
			if (!base)
				base = tm[SAMPLES / 2];
			if (f)
				printf("  %8.1f (%4.2f)", tm[SAMPLES / 2], tm[SAMPLES / 2] / base);
			else
				printf("  %15.1f", tm[SAMPLES / 2]);
		}
		printf("\n");
	}

	free(src);
	free(dst);
}

int test_func(memcpy_t func, int large)
{
	if (tests(func) || test_exhaustive(func) || test_overread(func))
		return 1;
	printf("  tests passed\n");

	if (large) {
		bench(func, 0);
		bench(func, 1);
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int opt, large = 0, small = 0, rnd = 0;
	char *only = NULL;

	while ((opt = getopt(argc, argv, "lsrf:")) != -1) {
		switch (opt) {
		case 'l':	/* the 128 MiB benchmark */
			large = 1;
			break;
		case 's':	/* the per-size benchmark */
			small = 1;
			break;
		case 'r':	/* the random length benchmark */
			rnd = 1;
			break;
		case 'f':	/* only these functions (comma separated tags) */
			only = optarg;
			break;
		default:
			fprintf(stderr, "usage: %s [-l] [-s] [-r] [-f tag,tag...]\n"
				"  no -l/-s/-r: run all benchmarks; libc is always included\n",
				argv[0]);
			return 1;
		}
	}
	if (!large && !small && !rnd)
		large = small = rnd = 1;

	for (size_t i = 1; only && i < NFUNCS; i++) {
		char list[256];

		snprintf(list, sizeof(list), ",%s,", only);
		char tag[32];
		snprintf(tag, sizeof(tag), ",%s,", memcpys[i].tag);
		if (!strstr(list, tag))
			memcpys[i].skip = 1;
	}

	signal(SIGILL, sigill_handler);

	for (size_t i = 0; i < NFUNCS; i++) {
		if (memcpys[i].skip)
			continue;
		printf("Testing %s...\n", memcpys[i].name);
		if (sigsetjmp(jmp_env, 1)) {
			printf("%s: SIGILL, skipping\n", memcpys[i].name);
			memcpys[i].skip = 1;
			continue;
		}
		if (test_func(memcpys[i].func, large))
			return 1;
	}

	if (small)
		bench_sizes();
	if (rnd) {
		bench_random();
		bench_spec();
	}

	return 0;
}
