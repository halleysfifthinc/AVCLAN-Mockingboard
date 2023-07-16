CC=avr-g++
CFLAGS=-c -g -Os -Wall  -mmcu=atmega328p
CFLAGS+=-fno-exceptions -fpermissive -ffunction-sections -fdata-sections -fno-threadsafe-statics
DEFS= -DF_CPU=16000000L -DARDUINO=10605 -DARDUINO_AVR_DUEMILANOVE -DARDUINO_ARCH_AVR
LFLAGS=-g -MMD -mmcu=atmega328p

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
	avrdude -C/home/allen/Programs/arduino-1.6.5/hardware/tools/avr/etc/avrdude.conf -patmega328p -cstk500v1 -P/dev/arduino -b19200 -D -U flash:w:sniffer.hex:i

upload-arduino: sniffer.hex
	avrdude -C/home/allen/Programs/arduino-1.6.5/hardware/tools/avr/etc/avrdude.conf -patmega328p -carduino -P/dev/arduino -b57600 -D -U flash:w:sniffer.hex:i

connect:
	@picocom --nolock -b 115200 /dev/arduino ||:

size:
	avr-size -C --mcu=atmega328p sniffer.elf
