CC = gcc
CFLAGS = -lm -L. -lftd2xx -Wl,-rpath /usr/local/lib

APP = usb_control

all: $(APP)

$(APP): usb_control.c	
	$(CC) -o $(APP) $(CFLAGS) usb_control.c	
	
clean:
	rm -f *.o ; rm $(APP)
