CFLAGS := -O2 -pipe -Wall
ASFLAGS := $(CFLAGS) -march=rv64gv

memcpys := memcpy_64x10.o

testmemcpy: main.o $(memcpys)
	$(CC) ${CFLAGS} $^ -o $@

test:: testmemcpy
	./testmemcpy

clean::
	$(RM) *.o testmemcpy
