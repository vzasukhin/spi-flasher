
compile:
	gcc -O3 -Wall -lusb-1.0 usb.c spi.c spi-nor.c main.c -o spi-flasher
