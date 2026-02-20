CFLAGS := -O2 -pipe -Wall -march=rv64g -ggdb3
ASFLAGS := $(CFLAGS)

memcpys_src_c := $(wildcard memcpy_*.c)
memcpys_src_s := $(wildcard memcpy_*.S)
memcpys_obj := $(memcpys_src_c:.c=.o) $(memcpys_src_s:.S=.o)

memcpy_v.o: ASFLAGS += -march=rv64gv

testmemcpy: main.o $(memcpys_obj)
	$(CC) ${CFLAGS} $^ -o $@

test:: testmemcpy
	./testmemcpy

clean::
	$(RM) *.o testmemcpy
