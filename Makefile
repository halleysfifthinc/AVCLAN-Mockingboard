CC=avr-g++
CFLAGS=-c -g -Os -Wall -fno-exceptions -fpermissive -ffunction-sections -fdata-sections -fno-threadsafe-statics -mmcu=atmega328p -DF_CPU=16000000L -DARDUINO=10605 -DARDUINO_AVR_DUEMILANOVE -DARDUINO_ARCH_AVR
LFLAGS=-g -MMD -mmcu=atmega328p -DF_CPU=16000000L -DARDUINO=10605 -DARDUINO_AVR_DUEMILANOVE -DARDUINO_ARCH_AVR

all: ToyotaAuxEnabler.hex

ToyotaAuxEnabler.hex: ToyotaAuxEnabler.elf
	avr-objcopy -j .text -j .data -O ihex ToyotaAuxEnabler.elf ToyotaAuxEnabler.hex

ToyotaAuxEnabler.elf: ToyotaAuxEnabler.o USART.o AVCLanDriver.o
	$(CC) $(LFLAGS) -o ToyotaAuxEnabler.elf ToyotaAuxEnabler.o USART.o AVCLanDriver.o

ToyotaAuxEnabler.o: ToyotaAuxEnabler.c GlobalDef.h USART.h AVCLanDriver.h
	$(CC) $(CFLAGS) ToyotaAuxEnabler.c

USART.o: USART.c USART.h GlobalDef.h
	$(CC) $(CFLAGS) USART.c

AVCLanDriver.o: AVCLanDriver.c GlobalDef.h USART.h AVCLanDriver.h
	$(CC) $(CFLAGS) AVCLanDriver.c

clean:
	rm *.o *.hex *.elf

upload: ToyotaAuxEnabler.hex
	avrdude -C/home/allen/Programs/arduino-1.6.5/hardware/tools/avr/etc/avrdude.conf -v -patmega328p -carduino -P/dev/arduino -b57600 -D -Uflash:w:ToyotaAuxEnabler.hex:i

.PHONY: upload
