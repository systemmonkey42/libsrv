# COPTS=-ggdb3 -DDEBUG
COPTS=
CC=gcc $(COPTS)

PREFIX=/usr/local


# Explicit linkage of resolv.conf is needed on most, but not all platforms
#
# Linux, Solaris, OpenBSD:
## RESOLV=-lresolv
# FreeBSD:
RESOLV=

all: tags example/hello_client example/hello_server libsrv.so libsrv.a

libsrv.o: libsrv.c include/arpa/srvname.h
	$(CC) -Iinclude -c libsrv.c

example/hello_client.o: example/hello_client.c include/arpa/srvname.h
	$(CC) -Iinclude -c example/hello_client.c -o example/hello_client.o

example/hello_server.o: example/hello_server.c include/arpa/srvname.h
	$(CC) -Iinclude -c example/hello_server.c -o example/hello_server.o

example/hello_client: example/hello_client.o libsrv.so
	$(CC) example/hello_client.o libsrv.so $(RESOLV) -o example/hello_client

example/hello_server: example/hello_server.o libsrv.so
	$(CC) example/hello_server.o libsrv.so $(RESOLV) -o example/hello_server

tags: libsrv.c example/hello_server.c example/hello_client.c
	ctags *.[ch]

clean:
	rm -f *.o example/hello_client example/hello_server libsrv.a libsrv.so*

distclean: clean
	rm -f tags

libsrv.a: libsrv.o
	ar -r libsrv.a libsrv.o

libsrv.so: libsrv.c include/arpa/srvname.h
	$(CC) -shared -Iinclude -o libsrv.so libsrv.c -lc

install: all
	install --strip example/hello_client example/hello_server $(PREFIX)/sbin
	install libsrv.so libsrv.a $(PREFIX)/lib
	rm -f $(PREFIX)/lib/libsrv.so.0 $(PREFIX)/lib/libsrv.so.0.0
	ln -s libsrv.so   $(PREFIX)/lib/libsrv.so.1
	ln -s libsrv.so.1 $(PREFIX)/lib/libsrv.so.1.0
	mkdir -p $(PREFIX)/include/arpa
	install include/arpa/srvname.h $(PREFIX)/include/arpa/srvname.h

uninstall:
	rm -f $(PREFIX)/sbin/hello_client $(PREFIX)/sbin/hello_server
	rm -f $(PREFIX)/lib/libsrv.so* $(PREFIX)/lib/libsrv.a
	rm -f $(PREFIX)/include/arpa/srvname.h

anew:	clean all
