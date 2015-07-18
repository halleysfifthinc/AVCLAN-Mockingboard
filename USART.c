/*--------------------------------------------------------------------------------------------------

  Name         :  USART.c

  Description  :  Utility functions for ATmega8 USART.

  Author       :  2004-10-22 - Louis Frigon

  History      :  2004-10-22 - First release (v0.1).

--------------------------------------------------------------------------------------------------*/
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdio.h>

#include "USART.h"

/*--------------------------------------------------------------------------------------------------

  Name         :  InitUSART

  Description  :  Performs USART initialization: 9600,N,8,1.

  Argument(s)  :  None.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
void InitUSART ( void )
{
    //  Disable USART while setting baud rate.
    UCSR0B = 0x00;
    UCSR0A = 0x00;

    //  8 data bit, 1 stop, no parity.
    UCSR0C = _BV(EEAR7) | _BV(UCSZ01) | _BV(UCSZ00);

    //  Set USART baud rate @ 9600. Divider is 103 @ 16 MHz.
    UBRR0L = 103;

    //  Enable internal pull-up on Rx pin.
    PORTD |= _BV(PD0);

    //  Enable Tx & Rx.
    UCSR0B = _BV(RXEN0) | _BV(TXEN0);
}

/*--------------------------------------------------------------------------------------------------

  Name         :  UsartIsChr

  Description  :  Return status of USART Rx buffer.

  Argument(s)  :  None.

  Return value :  0 if Rx buffer empty.

--------------------------------------------------------------------------------------------------*/
bool UsartIsChr ( void )
{
    return UCSR0A & _BV(RXC0);
}

/*--------------------------------------------------------------------------------------------------

  Name         :  UsartGetChr

  Description  :  Return character USART Rx buffer. Blocking until Rx buffer not empty.

  Argument(s)  :  None.

  Return value :  Character in Rx buffer.

--------------------------------------------------------------------------------------------------*/
char UsartGetChr ( void )
{
    while ( !UsartIsChr() );

    return UDR0;
}

/*--------------------------------------------------------------------------------------------------

  Name         :  UsartPutChr

  Description  :  Send a character through the USART.

  Argument(s)  :  c -> char to send.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
void UsartPutChr ( char c )
{
    //  Wait for transmit register to be empty.
    while ( !(UCSR0A & _BV(UDRE0)) );

    UDR0 = c;
}

/*--------------------------------------------------------------------------------------------------

  Name         :  UsartPutStr

  Description  :  Transmit a string on the serial port.

  Argument(s)  :  str -> pointer to string to send.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
void UsartPutStr ( char *str )
{
    while ( *str )
    {
        UsartPutChr( *str++ );
    }
}

/*--------------------------------------------------------------------------------------------------

  Name         :  UsartPutCStr

  Description  :  Transmit a string on the serial port.

  Argument(s)  :  str -> pointer to constant string to send (strings in Flash).

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
void UsartPutCStr ( const char *str )
{
    char c;

    while ( (c = pgm_read_byte_near( str++ )) )
    {
        UsartPutChr( c );
    }
}

/*--------------------------------------------------------------------------------------------------
                                         End of file.
--------------------------------------------------------------------------------------------------*/
