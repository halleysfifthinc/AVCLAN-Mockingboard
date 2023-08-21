CC=avr-gcc
CFLAGS=-std=gnu17 --param=min-pagesize=0 -c -g -Os -Wall -mmcu=attiny3216
CFLAGS+=-fno-exceptions -ffunction-sections -fdata-sections -fshort-enums
DEFS=-DF_CPU=16000000L
LFLAGS=-mmcu=attiny3216 -MMD

all: sniffer.hex

sniffer.hex: sniffer.elf
	avr-objcopy -j .text -j .data -O ihex sniffer.elf sniffer.hex

sniffer.elf: sniffer.o com232.o avclandrv.o GlobalDef.o
	$(CC) $(LFLAGS) -o sniffer.elf sniffer.o com232.o avclandrv.o GlobalDef.o

sniffer.o: sniffer.c GlobalDef.h com232.h avclandrv.h
	$(CC) $(CFLAGS) $(DEFS) sniffer.c

com232.o: com232.c com232.h GlobalDef.h
	$(CC) $(CFLAGS) $(DEFS) com232.c

avclandrv.o: avclandrv.c GlobalDef.h com232.h avclandrv.h
	$(CC) $(CFLAGS) $(DEFS) avclandrv.c

GlobalDef.o: GlobalDef.c GlobalDef.h
	$(CC) $(CFLAGS) $(DEFS) GlobalDef.c

clean::
	@rm -f *.o *.hex *.elf

.PHONY: upload connect size

upload-final: sniffer.hex
	avrdude -C/home/allen/Programs/arduino-1.6.5/hardware/tools/avr/etc/avrdude.conf -pattiny3216 -cstk500v1 -P/dev/arduino -b19200 -D -U flash:w:sniffer.hex:i

upload-arduino: sniffer.hex
	avrdude -C/home/allen/Programs/arduino-1.6.5/hardware/tools/avr/etc/avrdude.conf -pattiny3216 -carduino -P/dev/arduino -b57600 -D -U flash:w:sniffer.hex:i

connect:
	@picocom --nolock -b 115200 /dev/arduino ||:

size:
	@avr-size -G sniffer.hex
