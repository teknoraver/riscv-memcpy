CFLAGS := -O2 -pipe -Wall -march=rv64gc -ggdb3
ASFLAGS := $(CFLAGS)

memcpys_src_c := $(wildcard memcpy_*.c)
memcpys_src_s := $(wildcard memcpy_*.S)
memcpys_obj := $(memcpys_src_c:.c=.o) $(memcpys_src_s:.S=.o)

memcpy_v.o: ASFLAGS += -march=rv64gcv

testmemcpy: main.o $(memcpys_obj)
	$(CC) ${CFLAGS} $^ -o $@

test:: testmemcpy
	./testmemcpy

clean::
	$(RM) *.o testmemcpy len2

# fixed-length call loop, for perf stat: ./len2 <tag> <len> [doff] [soff]
len2: len2.o $(memcpys_obj)
	$(CC) ${CFLAGS} $^ -o $@
