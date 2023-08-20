/*--------------------------------------------------------------------------------------------------

  Name         :  GlobalDef.h

  Description  :  Global definitions.

  History      :  2004/04/06 - Created by Louis Frigon.

--------------------------------------------------------------------------------------------------*/
#ifndef _GLOBALDEF_H_
#define _GLOBALDEF_H_

#include <avr/io.h>

#define FALSE 0
#define TRUE (!FALSE)

// AVC LAN bus on AC2 (PA6/7)
// PA6 AINP0 +
// PA7 AINN1 -
#define INPUT_IS_SET (bit_is_set(AC2_STATUS, AC_STATE_bp))
#define INPUT_IS_CLEAR (bit_is_clear(AC2_STATUS, AC_STATE_bp))

#define sbi(port, bit) (port) |= (1 << (bit))  // Set bit (i.e. to 1)
#define cbi(port, bit) (port) &= ~(1 << (bit)) // Clear bit (i.e. set bit to 0)

typedef unsigned char byte;
typedef unsigned int word;

// max 10 events in fifo
extern byte EventCount;
extern byte EventCmd[10];
extern byte Event;

#define EV_NOTHING 0
#define EV_DISPLAY 1
#define EV_STATUS 4

// const
#define smYear 1
#define smMonth 2
#define smDay 3
#define smHour 4
#define smMin 5
#define smWDay 6

// #define STOPEvent  cbi(TIMSK, TOIE1); cbi(UCSRB, RXCIE);
// #define STARTEvent sbi(TIMSK, TOIE1); sbi(UCSRB, RXCIE);

extern byte showLog;
extern byte showLog2;

#endif //  _GLOBALDEF_H_
