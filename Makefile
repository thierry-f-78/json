CFLAGS = -g -O0 -fPIC

libjson.a: json.o
	$(AR) rcv $@ $^

clean:
	rm -f json json.o
