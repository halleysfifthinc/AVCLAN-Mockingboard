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

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sfr_defs.h>
#include <stdint.h>
#include <string.h>
#include <util/atomic.h>

#include "avclan_frame.h"
#include "avclan_phy.h"
#include "cdchanger.h"
#include "com232.h"
#include "mediacontrol.h"
#include "statustimer.h"

// F_CPU defined in timing.h and potentially needed by avr-libc (e.g. delay.h)
#include "timing.h"

/* Disable non-read related interrupts (USART RX, PIT, TCA) during AVCLAN reads.
 */
void AVCLAN_stopEvent() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    statustimer_disable();
    USART0.CTRLA &= ~USART_RXCIE_bm;
    mediacontrol_syncDuringMask();
  }
}

// Re-enable serial and periodic interrupts.
void AVCLAN_startEvent() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (AVCLAN_isPlaying()) // Reenable status interrupt if currently playing
      statustimer_enable();
    USART0.CTRLA |= USART_RXCIE_bm;
  }
}

uint8_t AVCLAN_readframe(AVCLAN_frame_t *frame, log_t print) {
  struct errtype {
    // Error enum is ordered such that a lower numeric value corresponds to more
    // successful read
    enum : uint8_t {
      NO_ERROR = 0x00,
      BAD_DATA_PARITY = 0x01,
      BAD_LENGTH_RANGE,
      BAD_LENGTH_PARITY,
      BAD_PERIPHERAL_PARITY,
      BAD_CONTROLLER_PARITY,
      BAD_CONTROL_PARITY,
      STARTBIT_TOO_SHORT,
      STARTBIT_TOO_LONG,
      LATCHED_COMPARATOR,
    } errno;
    union {
      uint8_t val; // BAD_LENGTH_RANGE: the out-of-range length value
      struct {
        uint16_t read_val;
        uint8_t parity; // received (bad) parity bit
      };
    };
  } err = {0};

  AVCLAN_stopEvent(); // disable timer1 interrupt

  uint8_t tmp = 0;

  uint16_t startbitlen = TCB1.CNT = 0;
  while (!BUS_IS_IDLE) {
    startbitlen = TCB1.CNT;
    if (startbitlen > (uint16_t)AVCLAN_STARTBIT_LOGIC_0 * 1.2) {
      err.errno = STARTBIT_TOO_LONG;
      while (!BUS_IS_IDLE) {
        // If bus is "driven" too long, assume the AC2 is latched (e.g.
        // because the bus is actually floating). Kick it if so.
        // This should prevent/resolve a flood of "STARTBIT_TOO_LONG" errors
        if (TCB1.CNT > (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 3)) {
          err.errno = LATCHED_COMPARATOR;
          PORTA.OUTSET = PIN7_bm; // preset high before enabling the driver
          PORTA.DIRSET = PIN7_bm; // drive (-) hard high
          TCB1.CNT = 0;
          while (!BUS_IS_IDLE && TCB1.CNT < (uint16_t)AVCLAN_BIT0_LOGIC_1) {
            // Wait a max of ~6μs until bus is idle
          }
          PORTA.DIRCLR = PIN7_bm; // back to high-Z comparator input
          PORTA.OUTCLR = PIN7_bm;
        }
      }
      goto handle_err;
    }
  }
  if (startbitlen < (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 0.8)) {
    err.errno = STARTBIT_TOO_SHORT;
    // We missed the beginning of this message; wait for it to finish (bus
    // continuously idle for >1 bit length) before returning, so we don't have
    // multiple false-starts while the in-progress message keeps sending more
    // bits.
    TCB1.CNT = 0;
    while (TCB1.CNT < (uint16_t)(AVCLAN_BIT_LENGTH_MAX * 1.2)) {
      if (!BUS_IS_IDLE)
        TCB1.CNT = 0;
    }
    goto handle_err;
  }
  // Otherwise that was a start bit

  AVCLAN_readbits(&tmp, 1);
  frame->is_unicast = tmp;

  uint8_t parity = AVCLAN_readbits(&frame->controller_addr, 12);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp &= 1)) {
    err.errno = BAD_CONTROLLER_PARITY;
    if (print.verbose) {
      err.read_val = frame->controller_addr;
      err.parity = tmp;
    }
    goto handle_err;
  }

  parity = AVCLAN_readbits(&frame->peripheral_addr, 12);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp &= 1)) {
    err.errno = BAD_PERIPHERAL_PARITY;
    if (print.verbose) {
      err.read_val = frame->peripheral_addr;
      err.parity = tmp;
    }
    goto handle_err;
  }

  bool shouldACK = !AVCLAN_ismuted() && (frame->peripheral_addr == DEVICE_ADDR);

  if (shouldACK)
    AVCLAN_sendbit_ACK();
  else
    AVCLAN_readbits(&tmp, 1);

  parity = AVCLAN_readbits(&frame->control, 4);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp &= 1)) {
    err.errno = BAD_CONTROL_PARITY;
    if (print.verbose) {
      err.read_val = frame->control;
      err.parity = tmp;
    }
    goto handle_err;
  } else if (shouldACK) {
    AVCLAN_sendbit_ACK();
  } else {
    AVCLAN_readbits(&tmp, 1);
  }

  parity = AVCLAN_readbyte(&frame->length);
  AVCLAN_readbits(&tmp, 1);
  if (parity != (tmp &= 1)) {
    err.errno = BAD_LENGTH_PARITY;
    if (print.verbose) {
      err.read_val = frame->length;
      err.parity = tmp;
    }
    goto handle_err;
  } else if (shouldACK) {
    AVCLAN_sendbit_ACK();
  } else {
    AVCLAN_readbits(&tmp, 1);
  }

  if (frame->length == 0 || frame->length > MAXMSGLEN) {
    err.errno = BAD_LENGTH_RANGE;
    err.val = frame->length;
    goto handle_err;
  }

  for (uint8_t i = 0; i < frame->length; i++) {
    parity = AVCLAN_readbyte(&frame->data[i]);
    AVCLAN_readbits(&tmp, 1);
    if (parity != (tmp &= 1)) {
      err.errno = BAD_DATA_PARITY;
      if (print.verbose) {
        err.read_val = frame->data[i];
        err.parity = tmp;
      }
      goto handle_err;
    } else if (shouldACK) {
      AVCLAN_sendbit_ACK();
    } else {
      AVCLAN_readbits(&tmp, 1);
    }
  }

  if (false) {
  handle_err:;
    AVCLAN_startEvent();
    RS232_Print("ERR(read): ");
    switch (err.errno) {
      case LATCHED_COMPARATOR: RS232_Print("latched comparator"); break;
      case STARTBIT_TOO_SHORT: RS232_Print("start bit too short"); break;
      case STARTBIT_TOO_LONG: RS232_Print("start bit too long"); break;
      case BAD_CONTROLLER_PARITY:
        RS232_Print("reading controller addr.");
        goto VERBOSE;
      case BAD_PERIPHERAL_PARITY:
        RS232_Print("reading peripheral addr.");
        goto VERBOSE;
      case BAD_CONTROL_PARITY: RS232_Print("reading control"); goto VERBOSE;
      case BAD_LENGTH_PARITY: RS232_Print("reading length"); goto VERBOSE;
      case BAD_LENGTH_RANGE:
        RS232_Print("bad length 0x");
        RS232_PrintHex4(err.val);
        break;
      case BAD_DATA_PARITY: RS232_Print("reading data"); goto VERBOSE;
      case NO_ERROR:
        __builtin_unreachable();
      VERBOSE:
        if (print.verbose) {
          RS232_Print("; read 0x");
          RS232_PrintHex(err.read_val);
          RS232_Print(" and got bad parity ");
          RS232_PrintHex4(err.parity);
        }
    }
    RS232_Print("\n");
  } else {
    AVCLAN_startEvent();
  }

  // Only print if some data has been correctly received
  if (print.print && (err.errno < STARTBIT_TOO_SHORT)) {
    if (err.errno > BAD_DATA_PARITY)
      frame->length = 0;
    AVCLAN_printframe(frame, print.binary);
  }

  return err.errno;
}

uint8_t AVCLAN_sendframe(const AVCLAN_frame_t *frame, log_t print) {
  struct errtype {
    // Error enum is ordered such that a lower numeric value corresponds to more
    // success
    enum : uint8_t {
      NO_ERROR = 0x00,
      NAK_DATA = 0x01,
      NAK_MESSAGE_LENGTH,
      NAK_CONTROL,
      NAK_ADDRESS,
      BUSY,
      MUTED,
    } errno;
    uint8_t val;
  } err = {0};

  if (AVCLAN_ismuted()) {
    err.errno = MUTED;
    goto handle_err;
  }

  AVCLAN_stopEvent();

  // wait for free line
  TCB1.CNT = 0;
  while (BUS_IS_IDLE) {
    // Wait for 120% of a bit length
    if (TCB1.CNT >= (uint16_t)(AVCLAN_BIT_LENGTH_MAX * 2))
      break;
  }

  // End of first loop could be due to bus being driven
  TCB1.CNT = 0;
  if (!BUS_IS_IDLE) {
    // Some other device started sending
    // Can't yet simultaneously send and recieve to do proper CSMA/CD
    err.errno = BUSY;
    goto handle_err;

    // Beginnings of CSMA/CD
    // do {
    //   if (TCB1.CNT >= (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 1.2))
    //     return 1; // Something's hinky; nothing is longer than the start bit
    // } while (!BUS_IS_IDLE);
    // if (TCB1.CNT <= (uint16_t)(AVCLAN_STARTBIT_LOGIC_0 * 0.8))
    //   return 1; // Shouldn't be possible (waiting 2 bit lengths with idle
    //   bus,
    //             // then next bit should be a long one ie start)
    // set_AVC_logic_for(1, AVCLAN_STARTBIT_LOGIC_1); // wait for end of start
    // bit
  } else {
    AVCLAN_sendbit(bit_start);
  }
  AVCLAN_sendbits(&(uint8_t){frame->is_unicast}, 1);

  avclan_bit_t parity = AVCLAN_sendbits(&frame->controller_addr, 12);
  AVCLAN_sendbit(parity);

  parity = AVCLAN_sendbits(&frame->peripheral_addr, 12);
  AVCLAN_sendbit(parity);

  if (frame->is_unicast && !AVCLAN_readbit_ACK()) {
    err.errno = NAK_ADDRESS;
    goto handle_err;
  }

  parity = AVCLAN_sendbits(&frame->control, 4);
  AVCLAN_sendbit(parity);

  if (frame->is_unicast && !AVCLAN_readbit_ACK()) {
    err.errno = NAK_CONTROL;
    goto handle_err;
  }

  parity = AVCLAN_sendbyte(&frame->length); // data length
  AVCLAN_sendbit(parity);

  if (frame->is_unicast && !AVCLAN_readbit_ACK()) {
    err.errno = NAK_MESSAGE_LENGTH;
    goto handle_err;
  }

  for (uint8_t i = 0; i < frame->length; i++) {
    parity = AVCLAN_sendbyte(&frame->data[i]);
    AVCLAN_sendbit(parity);
    // Based on the µPD6708 datasheet, ACK bit for broadcast doesn't seem
    // necessary (i.e. This deviates from the previous broadcast specific
    // function that sent an extra `1` bit after each byte/parity)
    if (frame->is_unicast && !AVCLAN_readbit_ACK()) {
      err.errno = NAK_DATA;
      err.val = i;
      goto handle_err;
    }
    // else
    //   AVCLAN_sendbit_1();
  }

  // back to read mode
  if (false) {
  handle_err:;
    AVCLAN_startEvent();
    RS232_Print("Error");
    switch (err.errno) {
      case MUTED: RS232_Print(": Device muted"); break;
      case BUSY: RS232_Print(": Busy bus"); break;
      case NAK_ADDRESS:
      case NAK_CONTROL:
      case NAK_MESSAGE_LENGTH:
      case NAK_DATA:
        RS232_Print(" NAK: ");
        switch (err.errno) {
          case NAK_ADDRESS: RS232_Print("address"); break;
          case NAK_CONTROL: RS232_Print("Control"); break;
          case NAK_MESSAGE_LENGTH: RS232_Print("Message length"); break;
          case NAK_DATA:
            RS232_Print(" data[");
            RS232_PrintDec(err.val);
            RS232_Print("]");
            break;
          case NO_ERROR:
          case MUTED:
          case BUSY: __builtin_unreachable();
        }
        break;
      case NO_ERROR: __builtin_unreachable();
    }
    RS232_Print("\n");
  } else {
    AVCLAN_startEvent();
  }

  if (print.print)
    AVCLAN_printframe(frame, print.binary);

  return err.errno;
}

void AVCLAN_printframe(const AVCLAN_frame_t *frame, bool binary) {
  if (binary) {
    uint8_t buffer[8];
    buffer[0] = 0x10; // Data Link Escape, signaling binary data forthcoming
    buffer[1] = frame->is_unicast;

    // Send addresses in big-endian order
    buffer[2] = (uint8_t)(frame->controller_addr >> 8);
    buffer[3] = (uint8_t)frame->controller_addr;
    buffer[4] = (uint8_t)(frame->peripheral_addr >> 8);
    buffer[5] = (uint8_t)frame->peripheral_addr;

    buffer[6] = frame->control;
    buffer[7] = frame->length;
    RS232_sendbytes((uint8_t *)&buffer, 8);
    RS232_sendbytes(frame->data, frame->length);

    buffer[0] = 0x17; // End of transmission block
    buffer[1] = 0x0D; // \r
    buffer[2] = 0x0A; // \n
    RS232_sendbytes((uint8_t *)&buffer, 3);
  } else {
    RS232_PrintHex4(frame->is_unicast);

    RS232_Print(" 0x");
    RS232_PrintHex12(frame->controller_addr);
    RS232_Print(" 0x");
    RS232_PrintHex12(frame->peripheral_addr);

    RS232_Print(" 0x");
    RS232_PrintHex4(frame->control);

    RS232_Print(" 0x");
    RS232_PrintHex4(frame->length);

    for (uint8_t i = 0; i < frame->length; i++) {
      RS232_Print(" 0x");
      RS232_PrintHex8(frame->data[i]);
    }
    RS232_Print("\n");
  }
}

uint8_t AVCLAN_parseframe(const uint8_t *bytes, uint8_t len,
                          AVCLAN_frame_t *frame) {
  struct errtype {
    enum : uint8_t {
      TOO_SHORT = 0x01,
      MISMATCH_LENGTH,
      LENGTH_TOO_BIG,
    } errno;
    uint8_t val;
  } err = {0};

  if (len < sizeof(AVCLAN_frame_t)) {
    err.errno = TOO_SHORT;
    goto handle_err;
  }
  const uint8_t *last = bytes + len;

  frame->is_unicast = *bytes++;
  frame->controller_addr = bytes[0] | ((uint16_t)bytes[1] << 8);
  bytes += 2;
  frame->peripheral_addr = bytes[0] | ((uint16_t)bytes[1] << 8);
  bytes += 2;
  frame->control = *bytes++;
  frame->length = *bytes++;

  if (frame->length > MAXMSGLEN) {
    err.errno = LENGTH_TOO_BIG;
    err.val = frame->length;
    goto handle_err;
  }

  if ((bytes + frame->length) <= last) {
    memcpy(frame->data, bytes, frame->length);
  } else {
    err.errno = MISMATCH_LENGTH;
    goto handle_err;
  }

  if (false) {
  handle_err:;
    RS232_Print("ERR(parse): ");
    switch (err.errno) {
      case TOO_SHORT:
        RS232_Print("not enough bytes too fill AVCLAN frame");
        break;
      case MISMATCH_LENGTH:
        RS232_Print("frame->length is longer than remaining data");
        break;
      case LENGTH_TOO_BIG:
        RS232_Print("frame->length exceeds MAXMSGLEN: 0x");
        RS232_PrintHex8(err.val);
        break;
      default: break;
    }
    RS232_Print("\n");
  }

  return err.errno;
}

#ifndef NDEBUG
  // Only used immediately below
  #define XSTR(x) #x
  #define STR(x)  XSTR(x)

uint16_t pulses[100];
uint16_t periods[100];

void AVCLan_Measure() {
  AVCLAN_stopEvent();

  uint8_t tmp = 0;

  RS232_Print(
      "Timing config: F_CPU=" STR(F_CPU) ", TCB_CLKSEL=" STR(TCB_CLKSEL) "\n");
  RS232_Print("Sampling bit (pulse-width and period) timing...\n");

  for (uint8_t n = 0; n < 100; n++) {
    while (pulse_count == tmp) {}
    pulses[n] = pulsewidth;
    periods[n] = period;
    tmp = pulse_count;
  }

  RS232_Print("Pulses:\n");
  for (uint8_t i = 0; i < 100; i++) {
    RS232_PrintHex8((uint8_t)(pulses[i] >> 8));
    RS232_PrintHex8((uint8_t)pulses[i]);
    RS232_Print("\n");
  }

  RS232_Print("Periods:\n");
  for (uint8_t i = 0; i < 100; i++) {
    RS232_PrintHex8((uint8_t)(periods[i] >> 8));
    RS232_PrintHex8((uint8_t)periods[i]);
    RS232_Print("\n");
  }
  RS232_Print("\nDone.\n");

  AVCLAN_startEvent();
}
#endif
