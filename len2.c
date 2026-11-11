#include <stdlib.h>
#include <string.h>
typedef void *(memcpy_t)(void *, const void *, size_t);
extern memcpy_t memcpy_shifted_noshort, memcpy_shifted, memcpy_shifted_bl2;
int main(int argc, char **argv)
{
	static char src[64], dst[64];
	memcpy_t *volatile vf = argv[1][0] == 'n' ? memcpy_shifted_noshort
			      : argv[1][0] == 's' ? memcpy_shifted
			      : argv[1][0] == '2' ? memcpy_shifted_bl2 : memcpy;
	memcpy_t *f = vf;
	size_t len = atoi(argv[2]);
	for (long i = 0; i < 100000000; i++)
		f(dst, src, len);
	return 0;
}
