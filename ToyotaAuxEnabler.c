/*--------------------------------------------------------------------------------------------------

  Name         :  ToyotaAuxEnabler.c

  Description  :  This program enables the AUX audio input on old Toyota radios with CD. Pressing
                  CD Eject button while no CD is loaded will toggle the AUX input.

  MCU          :  ATmega328P @ 16 MHz.

  Author       :  2007-01-27 - Louis Frigon

  Copyright    :  (c) 2007 SigmaObjects

  History      :  2007-01-27 - v0.1 Prototyping draft inspired from Marcin Slonicki's code.
                  2007-02-24 - v1.0 Production release.

--------------------------------------------------------------------------------------------------*/
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <stdio.h>

#include "GlobalDef.h"
#include "USART.h"
#include "AVCLanDriver.h"

#define FIRMWARE_VERSION    "v1.0"
#define FIRMWARE_DATE       __DATE__

/*--------------------------------------------------------------------------------------------------
                                         Prototypes
--------------------------------------------------------------------------------------------------*/
void InitMCU ( void );

/*--------------------------------------------------------------------------------------------------
                                      Global Variables
--------------------------------------------------------------------------------------------------*/
volatile bool   SecondsTick = FALSE;

char            UsartMsgBuffer[ USART_BUFFER_SIZE ];

/*--------------------------------------------------------------------------------------------------

  Name         :  InitMCU

  Description  :  Performs MCU initialization.

  Argument(s)  :  None.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
void InitMCU ( void )
{
    // Init LED port pin
    LED_DDR |= LEDOUT;
    LedOff();

    // OLD: TCCR0A = _BV(CS01);
    // Correction: TCCR0B has the CS0n bits, not TCCR0A, prescaler should be at 8 or less to have 1 or more counts/us
    // Timer 0 prescaler = 8 ( 2 count / us )
    TCCR0B = _BV(CS01);

    InitUSART();

    // Preset AVC bus driver output pins but leave pins tri-stated until we need to use them.
    PORTD |= _BV(PD3);   // PD3 (+) high.
    PORTD &= ~_BV(PD2);  // PD2 (-) low.

    //  Enable watchdog @ ~2 sec.
    // Likely this too, after research probably not, but maybe
    wdt_enable( WDTO_2S );
}

/*--------------------------------------------------------------------------------------------------

  Name         :  LedOn

  Description  :  Turn LED on (active low signal).

  Argument(s)  :  None.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
inline void LedOn ( void )
{
    LED_PORT &= ~LEDOUT;
}

/*--------------------------------------------------------------------------------------------------

  Name         :  LedOff

  Description  :  Turn LED off (active low signal).

  Argument(s)  :  None.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
inline void LedOff ( void )
{
    LED_PORT |= LEDOUT;
}

/*--------------------------------------------------------------------------------------------------

  Name         :  main

  Description  :  Program's main function.

  Argument(s)  :  None.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
int main ( void )
{
    InitMCU();

    UsartPutCStr( PSTR("\r\n\t\t     Toyota AVC-Lan AUX Enabler\r\n") );
    UsartPutCStr( PSTR("\t\tCopyright (C) 2007, SigmaObjects Inc.\r\n") );

    sprintf( UsartMsgBuffer, "\t\t     Firmware %s, %s\r\n\r\n", FIRMWARE_VERSION, FIRMWARE_DATE );
    UsartPutStr( UsartMsgBuffer );

    while ( 1 )
    {
        // Reset watchdog.
        wdt_reset();

        AvcActionID actionID = AvcReadMessage();

        if ( actionID != ACT_NONE )
        {
            AvcProcessActionID( actionID );
        }
    }
}

/*--------------------------------------------------------------------------------------------------
                                         End of file.
--------------------------------------------------------------------------------------------------*/
