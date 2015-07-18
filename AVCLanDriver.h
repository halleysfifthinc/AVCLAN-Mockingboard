/*--------------------------------------------------------------------------------------------------

  Name         :  AvcLanDriver.h

  Description  :  AVC Lan driver for Toyota devices

--------------------------------------------------------------------------------------------------*/
#ifndef _AVCLANDRV_H_
#define _AVCLANDRV_H_

#include "GlobalDef.h"

#define HU_ADDRESS                  0x190       // But possibly 0x160, known to be both

#define MY_ADDRESS                  0x360       // CD Changer #1

#define BROADCAST_ADDRESS           0x01FF      // All audio devices

#define CONTROL_FLAGS               0xF

/*--------------------------------------------------------------------------------------------------

                       |<---- Bit '0' ---->|<---- Bit '1' ---->|
     Physical '1'      ,---------------,   ,---------,         ,---------
                       ^               |   ^         |         ^
     Physical '0' -----'               '---'         '---------'--------- Idle low
                       |---- 33 us ----| 7 |- 20 us -|- 20 us -|

     A bit '0' is typically 33 us high followed by 7 us low.
     A bit '1' is typically 20 us high followed by 20 us low.
     A start bit is typically 165 us high followed by 30 us low.

--------------------------------------------------------------------------------------------------*/

// Following multipliers result of Timer 0 prescaler having 2 counts/us
#define NORMAL_BIT_LENGTH           74 //37*2

#define BIT_1_HOLD_ON_LENGTH        40 //20*2
#define BIT_0_HOLD_ON_LENGTH        64 //33*2

#define START_BIT_LENGTH            372 //186*2
#define START_BIT_HOLD_ON_LENGTH    336 //168*2

typedef enum
{   // No this is not a mistake, broadcast = 0!
    MSG_NORMAL      = 1,
    MSG_BCAST       = 0

} AvcTransmissionMode;

typedef enum
{
    ACT_NONE,

    ACT_AUX_IN_USE,
    ACT_TUNER_IN_USE,
    ACT_TAPE_IN_USE,
    ACT_CD_IN_USE,

    ACT_EJECT_CD,
    ACT_NO_CD,
    ACT_TUNER_INFO,
    ACT_AUDIO_STATUS,
//    ACT_CD_INFO,

    ACT_STATUS,
    ACT_REGISTER,
    ACT_INIT,
    ACT_CHECK

} AvcActionID;

typedef struct
{
    AvcActionID         ActionID;           // Action to perform after receiving this message.
    byte                DataSize;           // Payload data size (bytes).
    byte                Data[ 11 ];         // Payload data.
    char                Description[ 17 ];  // ASCII description of the command for terminal dump.

} AvcIncomingMessageStruct;

typedef const AvcIncomingMessageStruct AvcInMessage;

typedef struct
{
    AvcTransmissionMode Mode;               // Transmission mode: normal (1) or broadcast (0).
    byte                DataSize;           // Payload data size (bytes).
    byte                Data[ 11 ];         // Payload data.
    char                Description[ 17 ];  // ASCII description of the command for terminal dump.

} AvcOutgoingMessageStruct;

typedef const AvcOutgoingMessageStruct AvcOutMessage;

/*--------------------------------------------------------------------------------------------------
                                         Prototypes
--------------------------------------------------------------------------------------------------*/
AvcActionID     AvcReadMessage ( void );

bool            AvcProcessActionID ( AvcActionID actionID );
void            AvcUpdateStatus ( void );

void            DumpRawMessage ( bool incoming );

#endif // _AVCLANDRV_H_

/*--------------------------------------------------------------------------------------------------
                                         End of file.
--------------------------------------------------------------------------------------------------*/
