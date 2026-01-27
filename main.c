#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

extern void *__memcpy(void *, const void *, size_t);

int test(char *buf1, char *buf2, size_t size)
{
	for (int i = 0; i < size; i++)
		buf1[i] = random();

	__memcpy(buf2, buf1, size);

	if (memcmp(buf1, buf2, size))
		printf("!differ, size: %lu, alignment: %ld\n", size, (buf1 - buf2) & 7);

	return 0;
}

int main(int argc, char *argv[])
{
	char buf1[256], buf2[256];

	srandom(time(NULL));

	test(buf1, buf2, 0);

	test(buf1, buf2, sizeof(buf1));
	test(buf1, buf2, 16);
	test(buf1, buf2, 3);

	test(buf1+1, buf2, 200);
	test(buf1+1, buf2+1, 200);
	test(buf1, buf2+1, 200);
	test(buf1, buf2+5, 200);
	test(buf1+2, buf2+4, 200);

	test(buf1, buf2+1, 40);
	test(buf1+3, buf2+1, 40);

	return 0;
}
