/*--------------------------------------------------------------------------------------------------

  Name         :  USART.h

  Description  :  Header file for USART functions.

  History      :  2004-10-22 - Created by Louis Frigon.

--------------------------------------------------------------------------------------------------*/
#ifndef _USART_H_
#define _USART_H_

#include <avr/pgmspace.h>

#include "GlobalDef.h"

#define USART_BUFFER_SIZE       80

extern char          UsartMsgBuffer[ USART_BUFFER_SIZE ];

/*--------------------------------------------------------------------------------------------------
                                    Function prototypes
--------------------------------------------------------------------------------------------------*/
//  Function prototypes are mandatory otherwise the compiler generates unreliable code.

void InitUSART               ( void );
bool UsartIsChr              ( void );
char UsartGetChr             ( void );
void UsartPutChr             ( char c );
void UsartPutStr             ( char *str );
void UsartPutCStr            ( const char *str );


#endif   //  _USART_H_
/*--------------------------------------------------------------------------------------------------
                                         End of file.
--------------------------------------------------------------------------------------------------*/
