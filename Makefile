CFLAGS := -O2 -pipe -Wall
ASFLAGS := $(CFLAGS) -march=rv64gv

testmemcpy: main.o memcpy.o
	$(CC) ${CFLAGS} $^ -o $@

test:: testmemcpy
	./testmemcpy

clean::
	$(RM) *.o testmemcpy
