CC = gcc
CFLAGS = -Wall -O3
LIBS = -lm -L. -pthread -lftd2xx -Wl,-rpath /usr/local/lib -Wall

all: attrracd attrrac watchdog

attrracd: attrracd.o usb_control.o helper.o 
	$(CC) $(CFLAGS) $(LIBS) -o $@ $^	
	
attrrac: attrrac.o
	$(CC) $(CFLAGS) -o $@ $^	
		
watchdog: watchdog.c
	$(CC) -O -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o ; rm attrracd
