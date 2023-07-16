``` c
HU < (bro) 190| FFF| 11 01 00 // lan_reg
out > 00 01 11 10 63 // CMD_REGISTER
HU < (bro) 190| FFF| 11 01 45 60 // Tuner in use
HU < (bro) 190| FFF| 11 01 01 // lan_init
out > 63 31 F1 00 80 FF FF FF FF 00 80  // /*
out > 63 31 F3 00 3F 00 00 00 00 02
out > 63 31 F3 00 3F 00 01 00 01 02
out > 63 31 F3 00 3D 00 01 00 01 02
out > 63 31 F3 00 39 00 01 00 01 02       // Registration gobbledygook
out > 63 31 F3 00 31 00 01 00 01 02       // probably not all necessary
out > 63 31 F3 00 21 00 01 00 01 02
out > 63 31 F1 00 90 01 FF FF FF 00 80
out > 63 31 F3 00 01 00 01 00 01 02
out > 63 31 F1 00 30 01 FF FF FF 00 80 // */
HU < (dir) 190| 360| 00 31 63 E0  // Similar to play_req1
                                  // Logic device ID 31 => Me (63)
                                  // Command E0
                                  // What is logic device 31???

HU < (dir) 190| 360| 00 25 63 E4  // Also similar to play_req1

HU < (bro) 190| FFF| 01 01 59 39  // Nothing heard?
HU < (bro) 190| FFF| 01 01 59 39  // What is logic device 59???
HU < (dir) 190| 360| 00 31 63 E0

HU < (dir) 190| 360| 00 31 63 E0  // Try again?
HU < (dir) 190| 360| 00 31 63 E0
HU < (dir) 190| 360| 00 31 63 E2
HU < (dir) 190| 360| 00 31 63 E2
HU < (dir) 190| 360| 00 31 63 E2
HU < (dir) 190| 360| 00 31 63 E2
HU < (bro) 190| FFF| 11 01 00
out > 00 01 11 10 63
HU < (bro) 190| FFF| 11 01 45 60 // Tuner in use
HU < (bro) 190| FFF| 11 01 01
out > 63 31 F1 00 80 FF FF FF FF 00 80
out > 63 31 F3 00 3F 00 00 00 00 02
out > 63 31 F3 00 3F 00 01 00 01 02
out > 63 31 F3 00 3D 00 01 00 01 02
out > 63 31 F3 00 39 00 01 00 01 02
out > 63 31 F3 00 31 00 01 00 01 02
out > 63 31 F3 00 21 00 01 00 01 02
out > 63 31 F1 00 90 01 FF FF FF 00 80
out > 63 31 F3 00 01 00 01 00 01 02
out > 63 31 F1 00 30 01 FF FF FF 00 80
HU < (dir) 190| 360| 00 31 63 E0

HU < (dir) 190| 360| 00 25 63 E4

HU < (bro) 190| FFF| 01 01 59 39
HU < (bro) 190| FFF| 01 01 59 39
HU < (bro) 190| FFF| 11 01 45 60 // Tuner in use
HU < (bro) 190| FFF| 01 01 59 39
HU < (dir) 190| 360| 00 31 63 E0

HU < (dir) 190| 360| 00 31 63 E0
HU < (dir) 190| 360| 00 31 63 E0
HU < (dir) 190| 360| 00 25 63 80
out > 00 63 11 50 01
HU < (dir) 190| 360| 00 11 63 42 41

HU < (dir) 190| 360| 00 31 63 E2
HU < (dir) 190| 360| 00 11 63 42 41
HU < (dir) 190| 360| 00 31 63 E2
HU < (dir) 190| 360| 00 11 63 42 41
HU < (dir) 190| 360| 00 25 63 80
out > 00 63 11 50 01
HU < (dir) 190| 360| 00 31 63 E2
HU < (dir) 190| 360| 00 11 63 42 41
HU < (dir) 190| 360| 00 31 63 E2
HU < (dir) 190| 360| 00 25 63 80      // play_req1
out > 00 63 11 50 01                  // CMD_PLAY_OK1
HU < (dir) 190| 360| 00 11 63 43 01   // stop_req
out > 00 63 11 53 01                  // CMD_STOP1
out > 63 31 F1 00 30 01 01 00 00 00 80  // Player Status
HU < (bro) 190| FFF| 11 01 45 60 // Tuner in use



HU < (dir) 190| 360| 00 25 63 80      // play_req1
out > 00 63 11 50 01                  // CMD_PLAY_OK1
HU < (dir) 190| 360| 00 11 63 42 41   // play_req3 but without ending 00

HU < (dir) 190| 360| 00 11 63 42 41   // Trying again?
HU < (dir) 190| 360| 00 11 63 42 41
HU < (dir) 190| 360| 00 25 63 80
out > 00 63 11 50 01
HU < (dir) 190| 360| 00 25 63 80
out > 00 63 11 50 01
HU < (dir) 190| 360| 00 11 63 42 41
HU < (dir) 190| 360| 00 25 63 80
out > 00 63 11 50 01
HU < (dir) 190| 360| 00 11 63 43 01
out > 00 63 11 53 01
out > 63 31 F1 00 30 01 01 00 00 00 80
HU < (bro) 190| FFF| 11 01 45 60 // Tuner in use

HU < (dir) 190| 360| 00 25 63 80
out > 00 63 11 50 01
HU < (dir) 190| 360| 00 11 63 42 41

HU < (dir) 190| 360| 00 11 63 42 41
HU < (dir) 190| 360| 00 11 63 42 41
HU < (dir) 190| 360| 00 11 63 42 41
HU < (dir) 190| 360| 00 25 63 80
out > 00 63 11 50 01
HU < (dir) 190| 360| 00 11 63 43 01
out > 00 63 11 53 01
out > 63 31 F1 00 30 01 01 00 00 00 80
HU < (bro) 190| FFF| 11 01 45 60 // Tuner in use

HU < (dir) 190| 360| 00 25 63 80
out > 00 63 11 50 01
HU < (dir) 190| 360| 00 11 63 42 41

HU < (dir) 190| 360| 00 11 63 42 41
HU < (dir) 190| 360| 00 11 63 42 41
HU < (dir) 190| 360| 00 11 63 42 41
HU < (dir) 190| 360| 00 11 63 43 01
out > 00 63 11 53 01
out > 63 31 F1 00 30 01 01 00 00 00 80

```
