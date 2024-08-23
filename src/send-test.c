#include <stdint.h>
#include <stdio.h>

// uint8_t AVCLAN_sendbits(const uint8_t *byte, int8_t len) {
//   uint8_t b = *byte++;
//   uint8_t parity = 0;
//   int8_t len_mod8 = 0;
//   if (len != 8) {
//     len_mod8 = len & 0x7;
//     b <<= (uint8_t)(8 - len_mod8);
//   }

//   while (len > 0) {
//     len -= len_mod8;
//     for (; len_mod8 > 0; len_mod8--) {
//       if (b & 0x80) {
//         printf("1");
//         // AVCLan_Send_Bit1();
//         parity++;
//       } else {
//         printf("0");
//         // AVCLan_Send_Bit0();
//       }
//       b <<= 1;
//     }
//     len_mod8 = 8;
//     b = *byte++;
//   }
//   return (parity & 1);
// }

uint32_t AVCLAN_sendbits(const uint32_t *byte, int32_t len) {
  uint32_t b = *byte;
  uint32_t parity = 0;
  int32_t len_mod8 = 8;
  if (len & 0x7) {
    len_mod8 = len & 0x7;
    b <<= (8 - len_mod8);
  }

  while (len > 0) {
    len -= len_mod8;
    for (; len_mod8 > 0; len_mod8--) {
      if (b & 0x80) {
        printf("1");
        // AVCLan_Send_Bit1();
        parity++;
      } else {
        printf("0");
        // AVCLan_Send_Bit0();
      }
      b <<= 1;
    }
    len_mod8 = 8;
    b = *--byte;
    // byte++;
  }
  return (parity & 1);
}

uint32_t AVCLan_Send_Byte(uint32_t byte, uint32_t len) {
  uint32_t parity = 0;
  uint32_t b;
  if (len == 8) {
    b = byte;
  } else {
    b = byte << (8 - len);
  }

  while (1) {
    if ((b & 0x80) != 0) {
      printf("1");
      //   AVCLan_Send_Bit1();
      parity++;
    } else {
      printf("0");
      //   AVCLan_Send_Bit0();
    }
    len--;
    if (!len) {
      // if (!BUS_IS_IDLE) RS232_Print("SBER\n"); // Send Bit ERror
      return 1;
    }
    b = b << 1;
  }
}

void Send12BitWord(uint32_t data) {
  uint32_t parity = 0;
  // Most significant bit out first.
  for (uint32_t nbBits = 0; nbBits < 12; nbBits++) {
    // // Reset timer to measure bit length.
    // TCNT0 = 0;
    // // Drive output to signal high.
    // DDRD |= _BV(PD2) | _BV(PD3);
    if (data & 0x0800) {
      printf("1");
      parity = !parity;
      //   while (TCNT0 < BIT_1_HOLD_ON_LENGTH) {}
    } else {
      printf("0");
      //   while (TCNT0 < BIT_0_HOLD_ON_LENGTH) {}
    }

    // // Release output.
    // DDRD &= ~(_BV(PD2) | _BV(PD3));

    // // Hold output low until end of bit.
    // while (TCNT0 < NORMAL_BIT_LENGTH) {}

    data <<= 1; // Fetch next bit.
  }
}

void Send8BitWord(uint32_t data) {
  uint32_t parity = 0;

  // Most significant bit out first.
  for (int nbBits = 0; nbBits < 8; nbBits++) {
    // // Reset timer to measure bit length.
    // TCNT0 = 0;
    // // Drive output to signal high.
    // DDRD |= _BV(PD2) | _BV(PD3);

    if (data & 0x80) {
      printf("1");
      // Adjust parity.
      parity = !parity;
      //   while (TCNT0 < BIT_1_HOLD_ON_LENGTH) {}
    } else {
      printf("0");
      //   while (TCNT0 < BIT_0_HOLD_ON_LENGTH) {}
    }

    // // Release output.
    // DDRD &= ~(_BV(PD2) | _BV(PD3));
    // // Hold output low until end of bit.
    // while (TCNT0 < NORMAL_BIT_LENGTH) {}

    // Fetch next bit.
    data <<= 1;
  }
}

void Send4BitWord(uint32_t data) {
  uint32_t parity = 0;

  // Most significant bit out first.
  for (int nbBits = 0; nbBits < 4; nbBits++) {
    // // Reset timer to measure bit length.
    // TCNT0 = 0;
    // // Drive output to signal high.
    // DDRD |= _BV(PD2) | _BV(PD3);

    if (data & 0x8) {
      printf("1");
      // Adjust parity.
      parity = !parity;
      //   while (TCNT0 < BIT_1_HOLD_ON_LENGTH) {}
    } else {
      printf("0");
      //   while (TCNT0 < BIT_0_HOLD_ON_LENGTH) {}
    }

    // // Release output.
    // DDRD &= ~(_BV(PD2) | _BV(PD3));
    // // Hold output low until end of bit.
    // while (TCNT0 < NORMAL_BIT_LENGTH) {}

    // Fetch next bit.
    data <<= 1;
  }
}

int main(int argc, char *argv[]) {
  uint32_t n[] = {0xaa, 0x61, 0x03, 0xff};
  uint32_t *id = &n[2];
  uint32_t n3 = 0x03;
  uint32_t n63 = 0x63;

  printf("4 bits:\n----------------           0x%x", n3);
  printf("\nAVCLAN_sendbits(0x03, 4):  0b");
  AVCLAN_sendbits(&n3, 4);
  printf("\nAVCLan_Send_Byte(0x03, 4): 0b");
  AVCLan_Send_Byte(n3, 4);
  printf("\nSend4BitWord(0x03):        0b");
  Send4BitWord(n3);
  printf("\n\n");

  printf("8 bits:\n---------------");
  printf("\nAVCLAN_sendbits(0x63, 8):  0b");
  AVCLAN_sendbits(&n[1], 7);
  printf("\nAVCLAN_sendbits(0x63, 8):  0b");
  AVCLAN_sendbits(&n[1], 8);
  printf("\nAVCLAN_sendbits(0x63, 8):  0b");
  AVCLAN_sendbits(&n[2], 9);
  printf("\nAVCLAN_sendbits(0x63, 8):  0b");
  AVCLAN_sendbits(&n[2], 10);
  printf("\nAVCLAN_sendbits(0x63, 8):  0b");
  AVCLAN_sendbits(&n[2], 11);
  printf("\nAVCLAN_sendbits(0x63, 8):  0b");
  AVCLAN_sendbits(&n[2], 12);
  printf("\nAVCLan_Send_Byte(0x63, 8): 0b");
  AVCLan_Send_Byte(n[2], 12);
  printf("\nSend8BitWord(0x63):        0b");
  Send8BitWord(n[1]);
  printf("\n\n");

  printf("12 bits:\n---------------");
  printf("\nAVCLAN_sendbits(0x360, 12):  0b");
  AVCLAN_sendbits(id, 12);
  printf("\nAVCLan_Send_Byte(0x360, 12): 0b");
  AVCLan_Send_Byte(n[2], 4);
  AVCLan_Send_Byte(n[1], 8);
  printf("\nSend12BitWord(0x360):        0b");
  Send12BitWord(0x360);
  printf("\n\n");

  //   printf("\nAVCLAN_sendbits(0x60, 8): 0b");
  //   AVCLAN_sendbits((uint8_t *)0x60, 8);
  // printf("sizeof(id): %ld\n", sizeof(id));
  // printf("\nAVCLAN_sendbits(0x360, 12): 0b");
  // AVCLAN_sendbits(id, 12);
  // AVCLAN_sendbits((uint32_t[]){0x03, 0x60}, 12);
}