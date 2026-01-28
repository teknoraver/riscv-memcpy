CFLAGS := -O2 -pipe -Wall
ASFLAGS := $(CFLAGS)

testmemcpy: main.o memcpy.o
	$(CC) ${CFLAGS} $^ -o $@

test:: testmemcpy
	./testmemcpy

clean::
	$(RM) *.o testmemcpy
