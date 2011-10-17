CC=gcc
CFLAGS+=-Os -Wall
LDFLAGS+=-static
LDLIBS+=-lutil
all:	econsole egetty
econsole:	econsole.o skbuff.o jelopt.o
egetty:	egetty.o skbuff.o
clean:	
	rm -f *.o econsole egetty