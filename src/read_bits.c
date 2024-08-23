#include <avr/io.h>
#include <avr/sfr_defs.h>

// char AC0_STATUS_STATE_bm = (1<<4);

#define MAXMSGLEN 32

uint8_t message[32];
uint8_t current_uint8_t;
uint8_t nbits;

typedef struct AVCLAN_frame_struct {
  uint16_t sender_addr;   // formerly "master"
  uint16_t receiver_addr; // formerly "slave"
  uint8_t control;
  uint8_t length;
  uint8_t data[MAXMSGLEN];
} AVCLAN_frame_t;

int8_t AVCLAN_read_data(uint8_t *data, uint8_t nbits) {
  int8_t parity = 0;
  uint8_t bits = 0;
  while (bits < nbits) {
    loop_until_bit_is_set(TCB0.INTFLAGS, 0);
    uint16_t period = TCB0.CNT;
    uint16_t pulse = TCB0.CCMP;
    if ((period - pulse) < 0x033) {
      (*data)++;
      parity++;
    }
    bits++;
    if ((bits ^ 0x08) == 1) {
      data++;
    }
  }
  return parity;
}

int8_t AVCLAN_read_data_asm(uint8_t *data, uint8_t nbits) {
  int8_t parity = 0;
  uint8_t bits = 0;
  uint8_t uint8_t = data[0];
  while (bits < nbits) {
    loop_until_bit_is_set(TCB0.INTFLAGS, 0);
    __asm__ __volatile__(
        "sub  %A[tcb],%C[tcb]       ; TCB0_CNT - TCB0_CCMP (low) \n\t"
        "sbc  %B[tcb],%D[tcb]       ; TCB0_CNT - TCB0_CCMP (high) \n\t"
        "cp   %A[tcb],lo8(thresh)   ; Compare low \n\t"
        "cpc  %B[tcb],hi8(thresh)   ; Compare high; carry flag is set if "
        "(TCB_CNT - TCB_CCMP) < half_per (ie read bit is 0x01)\n\t"
        "brcc skipparity            ; Skip incrementing parity if bit was 0 "
        "\n\t"
        "inc  %[parity]             ; Increment parity (doesn't change carry "
        "flag) \n"
        "skipparity:                     \n\t"
        "rol  %[data]               ; Rotate left with carry flag \n\t"
        "inc  %[bits]               ; Increment \n\t"
        "mov  __tmp_reg__,%[bits]    ; Copy to temp registry to avoid messing "
        "with loop count \n\t"
        "eor  __tmp_reg__,$0x09      ; Reset if greater than 8 \n\t"
        "brbc 1,end                  ; The current uint8_t read was just "
        "completed; increment the pointer \n\t"
        "std  %a[data_ADDR]+,%[data] ; \n"
        "end:                            \n\t"
        : [data_ADDR] "+e"(data), [bits] "+r"(bits), [parity] "+r"(parity)
        : [tcb] "r"(_SFR_MEM32(
              &TCB0_CNT)), // TCB0_CNT and TCB0_CCMP are consecutive uint16_t.
                           // Load address of TCB0_CNT and access both words
                           // using offsets from that pointer
          [data] "r"(uint8_t),
          [thresh] "M"(0x0033) // Length of synchronization period (~20us) plus
                               // fudge factor to distinguish `0` (~20us +
                               // ~13us) from `1` (~20us)
        : "memory");
  }
  return parity;
}

int main() {
  uint8_t message[2];
  uint16_t master;

  int8_t parity = AVCLAN_read_data((uint8_t *)(&message), 16);
  parity = AVCLAN_read_data_asm((uint8_t *)(&master), 12);
}