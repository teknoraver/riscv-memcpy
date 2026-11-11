/* Call one memcpy 10^8 times with a fixed length, for perf stat:
   ./len2 <libc|shifted|bl2|bits|jt> <len> [dst offset] [src offset]  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(memcpy_t)(void *, const void *, size_t);
extern memcpy_t memcpy_shifted, memcpy_shifted_bl2, memcpy_shifted_bits,
	memcpy_shifted_jt;

static const struct { const char *tag; memcpy_t *func; } funcs[] = {
	{ "libc", memcpy },
	{ "shifted", memcpy_shifted },
	{ "bl2", memcpy_shifted_bl2 },
	{ "bits", memcpy_shifted_bits },
	{ "jt", memcpy_shifted_jt },
};

int main(int argc, char **argv)
{
	static char src[4096] __attribute__((aligned(64)));
	static char dst[4096] __attribute__((aligned(64)));
	memcpy_t *volatile vf = NULL;

	if (argc < 3)
		goto usage;
	for (size_t i = 0; i < sizeof(funcs) / sizeof(funcs[0]); i++)
		if (!strcmp(argv[1], funcs[i].tag))
			vf = funcs[i].func;
	if (!vf)
		goto usage;

	memcpy_t *f = vf;
	size_t len = atoi(argv[2]);
	char *d = dst + (argc > 3 ? atoi(argv[3]) : 0);
	char *s = src + (argc > 4 ? atoi(argv[4]) : 0);

	for (long i = 0; i < 100000000; i++)
		f(d, s, len);
	return 0;

usage:
	fprintf(stderr, "usage: %s <libc|shifted|bl2|bits|jt> <len> [doff] [soff]\n", argv[0]);
	return 1;
}
