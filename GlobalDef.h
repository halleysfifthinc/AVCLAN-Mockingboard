/*--------------------------------------------------------------------------------------------------

  Name         :  GlobalDef.h

  Description  :  Global definitions.

  History      :  2004/04/06 - Created by Louis Frigon.

--------------------------------------------------------------------------------------------------*/
#ifndef _GLOBALDEF_H_
#define _GLOBALDEF_H_

#include <avr/io.h>

/*--------------------------------------------------------------------------------------------------
                                          Constants
--------------------------------------------------------------------------------------------------*/
#define FALSE                   0
#define TRUE                    (!FALSE)

// AVC LAN bus directly connected to internal analog comparator (PD6/7)
// PD6 AIN0 +
// PD7 AIN1 -
#define DATAIN_PIN              ACSR
#define DATAIN                  ACO

#define INPUT_IS_SET            ( bit_is_set( DATAIN_PIN, DATAIN ) )
#define INPUT_IS_CLEAR          ( bit_is_clear( DATAIN_PIN, DATAIN ) )

#define LED_DDR                 DDRB
#define LED_PORT                PORTB
#define LEDOUT                  _BV(PORT5)

#define sbi(port, bit) (port) |= (1 << (bit))  // Set bit (i.e. to 1)
#define cbi(port, bit) (port) &= ~(1 << (bit)) // Clear bit (i.e. set bit to 0)

/*--------------------------------------------------------------------------------------------------
                                       Type definitions
--------------------------------------------------------------------------------------------------*/
typedef unsigned char           byte;
typedef unsigned int            word;

/*--------------------------------------------------------------------------------------------------
                                         Prototypes
--------------------------------------------------------------------------------------------------*/
inline void LedOff( void );
inline void LedOn( void );

/*--------------------------------------------------------------------------------------------------
                                    From Marcin's "const.h"
--------------------------------------------------------------------------------------------------*/

// max 10 events in fifo
extern byte EventCount;
extern byte EventCmd[10];
extern byte Event;

#define EV_NOTHING	0
#define EV_DISPLAY	1
#define EV_STATUS	4

//------------------------------------------------------------------------------

// const
#define smYear		1
#define smMonth		2
#define smDay		3
#define smHour		4
#define smMin		5
#define smWDay		6



//#define STOPEvent  cbi(TIMSK, TOIE1); cbi(UCSRB, RXCIE);
//#define STARTEvent sbi(TIMSK, TOIE1); sbi(UCSRB, RXCIE);


extern byte showLog;
extern byte showLog2;

#endif   //  _GLOBALDEF_H_

/*--------------------------------------------------------------------------------------------------
                                         End of file.
--------------------------------------------------------------------------------------------------*/
