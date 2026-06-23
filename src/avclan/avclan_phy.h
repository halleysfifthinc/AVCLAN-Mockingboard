/*
                        AVCLAN-Mockingboard
    Copyright (C) 2015 Allen Hill <allenofthehills@gmail.com>

    Portions of the following source code are based on code that is
    copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>
    copyright (C) 2007 Louis Frigon

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// AVC-LAN PHY: bus bit-banging via TCB timers + analog comparator AC2.
// This is the lowest layer and has no dependencies on the higher layers
// (frame / protocol / cdchanger) — keep it that way.

#ifndef AVCLAN_PHY_H
#define AVCLAN_PHY_H

#include <avr/io.h>
#include <avr/sfr_defs.h>
#include <stdint.h>

#include "avclan_defs.h"

// AVC LAN bus on AC2 (PA6/7)
// PA6 AINP0 +
// PA7 AINN1 -
#define BUS_IS_IDLE (bit_is_clear(AC2_STATUS, AC_STATE_bp))

typedef enum avclan_bit : uint8_t {
  bit_zero = 0x00,
  bit_one = 0x01,
  bit_start = 0x10
} avclan_bit_t;

// One-time hardware bring-up for the PHY: AC2, EVSYS, TCB0/TCB1, the AC inputs
// (PA6/7) and AC2-OUT LED (PB2). Leaves the bus idle and TX unmuted.
void AVCLAN_phyInit();

// Returns true if device TX is muted on AVCLAN bus
static inline bool AVCLAN_ismuted() {
  return (((VPORTA_DIR & PIN4_bm) | (VPORTA_DIR & PIN0_bm)) == 0);
}

// Mute device TX on AVCLAN bus
void AVCLAN_muteDevice(bool mute);

void AVCLAN_sendbit(avclan_bit_t bit);
void AVCLAN_sendbit_ACK();
uint8_t AVCLAN_readbit_ACK();

avclan_bit_t AVCLAN_sendbitsi(const uint8_t *bits, int8_t len);
avclan_bit_t AVCLAN_sendbitsl(const uint16_t *bits, int8_t len);
avclan_bit_t AVCLAN_sendbyte(const uint8_t *byte);

uint8_t AVCLAN_readbitsi(uint8_t *bits, uint8_t len);
uint8_t AVCLAN_readbitsl(uint16_t *bits, int8_t len);
uint8_t AVCLAN_readbyte(uint8_t *byte);

#define AVCLAN_sendbits(bits, len)                                             \
  _Generic((bits),                                                             \
      const uint16_t *: AVCLAN_sendbitsl,                                      \
      uint16_t *: AVCLAN_sendbitsl,                                            \
      const uint8_t *: AVCLAN_sendbitsi,                                       \
      uint8_t *: AVCLAN_sendbitsi)(bits, len)

#define AVCLAN_readbits(bits, len)                                             \
  _Generic((bits),                                                             \
      const uint16_t *: AVCLAN_readbitsl,                                      \
      uint16_t *: AVCLAN_readbitsl,                                            \
      const uint8_t *: AVCLAN_readbitsi,                                       \
      uint8_t *: AVCLAN_readbitsi)(bits, len)

#ifndef NDEBUG
// Bit-timing capture, populated by the TCB0 capture ISR; read by AVCLan_Measure.
extern volatile uint16_t pulsewidth;
extern volatile uint8_t pulse_count;
extern volatile uint16_t period;
#endif

#endif // AVCLAN_PHY_H
