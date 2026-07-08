# Hardware

I ordered the boards partially assembled from JLCPCB (to keep costs down, I ordered and soldered some non-standard/stocked parts myself.)

The non-populated/assembled BOM is:

| Mouser #:           | Mfr. #:          | Desc.:                                                                                                       | Order Qty. | Unit Price (USD) |
| ------------------- | ---------------- | ------------------------------------------------------------------------------------------------------------ | ---------- | ----------- |
| 579-ATTINY3216-SNR  | ATTINY3216-SNR   | 8-bit Microcontrollers - MCU 8-bit Microcontrollers - MCU 20MHz, 32KB, SOIC20, Ind 105C, Green, T&R          | 1          | $1.27       |
| 523-L717SDE09PA4CH4 | L717SDE09PA4CH4F | D-Sub Standard Connectors D-Sub Standard Connectors D SUB R/A                                                | 1          | $1.98       |
| 490-SJ-43514        | SJ-43514         | Phone Connectors Phone Connectors audio jack, 3.5 mm, rt, 4 conductor, through hole, 0 switches              | 1          | $1.35       |
| 667-ERZ-V20D220     | ERZ-V20D220      | Varistors Varistors 22V 2000A ZNR SUR ABSORBER 20MM                                                          | 1          | $1.24       |
| 538-22-28-8093      | 22-28-8093       | Headers & Wire Housings Headers & Wire Housings 2.54MM BREAKAWAY RA 9 CKT Gold                               | 2          | $0.76       |
| 865-XC6701D502JR-G  | XC6701D502JR-G   | LDO Voltage Regulators LDO Voltage Regulators 28V High Speed Voltage Regulator                               | 1          | $1.36       |
| 563-EXN-23350-BK    | EXN-23350-BK     | Enclosures, Boxes, & Cases Enclosures, Boxes, & Cases Extruded Aluminum Enclosure Black (1.4 X 2.7 X 1.9 In) | 1          | $14.40      |

An earlier version of the board* lacked cutouts in the corners to fit the ends of the intended housing, so I haven't used the listed enclosure (yet).

*Only version of the board I have ordered so far.

### Flashing

The CMake target `flash` uses the AVRDude utility using the "serialupdi" programmer type. I use a [USB => Serial converter](https://www.adafruit.com/product/5335) with the Rx and Tx lines connected, using one of the options described [by SpenceKonde here](https://github.com/SpenceKonde/AVR-Guidance/blob/master/UPDI/jtag2updi.md).
