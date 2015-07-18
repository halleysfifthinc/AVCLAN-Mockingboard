/*--------------------------------------------------------------------------------------------------

  Name         :  AVCLanDriver.c

  Description  :  AVC Lan driver for Toyota devices.

  Author       :  Louis Frigon

  Copyright    :  (c) 2007 SigmaObjects

----------------------------------------------------------------------------------------------------
                                          AVC LAN Theory

     The AVC bus is an implementation of the IEBus which is a differential line, floating on logical
     level '1' and driving on logical '0'. Floating level shall be below 20 mV whereas driving level
     shall be above 120 mV.

     The diagram below represents how things work from a logical perspective on the bus.

     A rising edge indicates a new bit. The duration of the high state tells whether it is a start
     bit (~165 us), a bit '0' (~30 us) or a bit '1' (~20 us). A normal bit length is close to 40 us.

                       |<---- Bit '0' ---->|<---- Bit '1' ---->|
     Physical '1'      ,---------------,   ,---------,         ,---------
                       ^               |   ^         |         ^
     Physical '0' -----'               '---'         '---------'--------- Idle low
                       |---- 32 us ----| 7 |- 20 us -|- 19 us -|

     A bit '1' is typically 20 us high followed by 19 us low.

     A bit '0' is typically 32 us high followed by 7 us low. A bit '0' is dominant i.e. it takes
     precedence over a '1' by extending the pulse. This is why lower addresses win on arbitration.

     A start bit is typically 165 us high followed by 30 us low.

                                  AVC LAN Frame Format
     Bits Description

      1   Start bit
      1   MSG_NORMAL
      12  Master address
      1   Parity
      12  Slave address
      1   Parity
      1   * Acknowledge * (read below)
      4   Control
      1   Parity
      1   * Acknowledge * (read below)
      8   Payload length (n)
      1   Parity
      1   * Acknowledge * (read below)
          8   Data
          1   Parity
          1   * Acknowledge * (read below)
      repeat 'n' times

     In point-to-point communication, sender issues an ack bit with value '1' (20 us). Receiver
     upon acking will extend the bit until it looks like a '0' (32 us) on the bus. In broadcast
     mode, receiver disregards the bit.

     An acknowledge bit of value '0' means OK, '1' means no ack.

--------------------------------------------------------------------------------------------------*/
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <stdio.h>

#include "GlobalDef.h"
#include "USART.h"
#include "AVCLanDriver.h"

/*--------------------------------------------------------------------------------------------------
                                      Local Functions
--------------------------------------------------------------------------------------------------*/
static void         SendStartBit ( void );
static void         Send12BitWord ( word data );
static void         Send8BitWord ( byte data );
static void         Send4BitWord ( byte data );
static void         Send1BitWord ( bool data );
static bool         SendMessage ( void );

static word         ReadBits ( byte nbBits );
static bool         ReadAcknowledge ( void );

static bool         HandleAcknowledge ( void );
static bool         IsAvcBusFree ( void );

static AvcActionID  GetActionID ( void );
static void         LoadDataInGlogalRegisters ( AvcOutMessage * msg );

/*--------------------------------------------------------------------------------------------------
                                      Global Variables
--------------------------------------------------------------------------------------------------*/
// Message frame global registers
static const char * Description;
static bool         Broadcast;
static word         MasterAddress;
static word         SlaveAddress;
static byte         Control;
static byte         DataSize;
static bool         ParityBit;
static byte         Data[ 256 ];

bool         AUX_Enabled     = FALSE;
AvcActionID  DeviceEnabled   = ACT_NONE; //casting possibly unneccesary

static AvcInMessage MessageTable [] PROGMEM =
{
/*--------------------------------------------------------------------------------------------------
                                  Head Unit (HU) Messages
    0x60 = Tuner ID
    0x61 = Tape ID
    0x62 = CD ID
    0x63 = CD Changer ID (this is what we're emulating)
--------------------------------------------------------------------------------------------------*/
    { ACT_AUX_IN_USE,   4, {0x11, 0x01, 0x45, 0x01}, "AUX in use" },
    { ACT_TUNER_IN_USE, 4, {0x11, 0x01, 0x45, 0x60}, "Tuner in use" },
    { ACT_TAPE_IN_USE,  4, {0x11, 0x01, 0x45, 0x61}, "Tape in use" },
    { ACT_CD_IN_USE,    4, {0x11, 0x01, 0x45, 0x62}, "CD in use" },

    { ACT_NONE,         3, {0x11, 0x01, 0x46}, "No device in use" },
    { ACT_NONE,         3, {0x11, 0x01, 0x20 /* xx */}, "Ping" }, // Get this once every minute in radio off mode. xx increments
    { ACT_TUNER_INFO, 5, {0x60, 0x31, 0xF1, 0x01, 0x01 /* xx xx xx 0x00 0x00 0x00 0x00 */ /* 81 0 C9 = 107.9 or 107.7*/}, "Tuner Status"},
    { ACT_EJECT_CD,    10, {0x62, 0x31, 0xF1, 0x00, 0x30, 0x01, 0x01, 0x00, 0x00, 0x00, 0x80}, "Eject CD" },
    { ACT_NO_CD,       10, {0x62, 0x31, 0xF1, 0x00, 0xF8, 0x01, 0x01, 0x00, 0x00, 0x00, 0x80}, "No CD" },
//    { ACT_CD_INFO,      6, {0x62, 0x31, 0xF1, 0x01, 0x10, 0x01 /* Track #, Min, Sec, 0x00, 0x80 */}, "CD Info: " },
    {ACT_AUDIO_STATUS, 4, { 0x74, 0x31, 0xF1, 0x90 /* Volume, Balance, Fade, Bass, 0x10, Treble, 0x00, 0x0F, 0x00, 0x00 */ }, "Audio Status"},

    { ACT_STATUS,       3, {0x00, 0x01, 0x0A}, "LAN Status" },
    { ACT_REGISTER,     3, {0x11, 0x01, 0x00}, "LAN Register" },
    { ACT_INIT,         3, {0x11, 0x01, 0x01}, "LAN Restart" },
    { ACT_CHECK,        3, {0x11, 0x01, 0x20}, "LAN Check" },

    { (AvcActionID)FALSE } //possibly should be ACT_NONE
};

const byte MessageTableSize = sizeof( MessageTable ) / sizeof( AvcInMessage );

/*--------------------------------------------------------------------------------------------------
                                    Our (CD) Commands
--------------------------------------------------------------------------------------------------*/
AvcOutMessage CmdReset    PROGMEM = { MSG_BCAST,  5, {0x00, 0x00, 0x00, 0x00, 0x00}, "Reset" }; // This causes HU to send ACT_REGISTER

//AvcOutMessage CmdRegister PROGMEM = { MSG_NORMAL, 5, {0x00, 0x01, 0x11, 0x10, 0x63}, "Register" };
//AvcOutMessage CmdRegister PROGMEM = { MSG_NORMAL, 5, {0x00, 0x01, 0x11, 0x54, 0x63}, "Toggle HU On/Off" };
//AvcOutMessage CmdRegister PROGMEM = { MSG_NORMAL, 5, {0x00, 0x01, 0x11, 0x54, 0x63}, "Toggle HU On/Off" };
AvcOutMessage CmdEnableAux  PROGMEM = { MSG_NORMAL, 5, {0x00, 0x01, 0x11, 0x50, 0x61}, "Enable AUX" };
AvcOutMessage CmdDisableAux PROGMEM = { MSG_NORMAL, 5, {0x00, 0x01, 0x11, 0x51, 0x61}, "Disable AUX" };

/*--------------------------------------------------------------------------------------------------

  Name         :  AvcRegisterMe

  Description  :  Sends registration message to master controller.

  Argument(s)  :  None.

  Return value :  (bool) -> TRUE if successful else FALSE.

--------------------------------------------------------------------------------------------------*/
bool AvcRegisterMe ( void )
{
    Broadcast     = MSG_NORMAL;
    MasterAddress = MY_ADDRESS;
    SlaveAddress  = HU_ADDRESS;
    Control       = CONTROL_FLAGS;

    AvcProcessActionID( ACT_REGISTER );

    return TRUE;
}

/*--------------------------------------------------------------------------------------------------

  Name         :  AvcReadMessage

  Description  :  Read incoming messages on the AVC LAN bus.

  Argument(s)  :  None.

  Return value :  (AvcActionID) -> Action ID associated with this message.

--------------------------------------------------------------------------------------------------*/
AvcActionID AvcReadMessage ( void )
{
    ReadBits( 1 ); // Start bit.

    LedOn();

    Broadcast = ReadBits( 1 );

    MasterAddress = ReadBits( 12 );
    bool p = ParityBit;
    if ( p != ReadBits( 1 ) )
    {
        UsartPutCStr( PSTR("AvcReadMessage: Parity error @ MasterAddress!\r\n") );
        return (AvcActionID)FALSE;
    }

    SlaveAddress = ReadBits( 12 );
    p = ParityBit;
    if ( p != ReadBits( 1 ) )
    {
        UsartPutCStr( PSTR("AvcReadMessage: Parity error @ SlaveAddress!\r\n") );
        return (AvcActionID)FALSE;
    }

    bool forMe = ( SlaveAddress == MY_ADDRESS );

    // In point-to-point communication, sender issues an ack bit with value '1' (20us). Receiver
    // upon acking will extend the bit until it looks like a '0' (32us) on the bus. In broadcast
    // mode, receiver disregards the bit.

    if ( forMe )
    {
        // Send ACK.
        Send1BitWord( 0 );
    }
    else
    {
        ReadBits( 1 );
    }

    Control = ReadBits( 4 );
    p = ParityBit;
    if ( p != ReadBits( 1 ) )
    {
        UsartPutCStr( PSTR("AvcReadMessage: Parity error @ Control!\r\n") );
        return (AvcActionID)FALSE;
    }

    if ( forMe )
    {
        // Send ACK.
        Send1BitWord( 0 );
    }
    else
    {
        ReadBits( 1 );
    }

    DataSize = ReadBits( 8 );
    p = ParityBit;
    if ( p != ReadBits( 1 ) )
    {
        UsartPutCStr( PSTR("AvcReadMessage: Parity error @ DataSize!\r\n") );
        return (AvcActionID)FALSE;
    }

    if ( forMe )
    {
        // Send ACK.
        Send1BitWord( 0 );
    }
    else
    {
        ReadBits( 1 );
    }

    byte i;

    for ( i = 0; i < DataSize; i++ )
    {
        Data[i] = ReadBits( 8 );
        p = ParityBit;
        if ( p != ReadBits( 1 ) )
        {
            sprintf( UsartMsgBuffer, "AvcReadMessage: Parity error @ Data[%d]\r\n", i );
            UsartPutStr( UsartMsgBuffer );
            return (AvcActionID)FALSE;
        }

        if ( forMe )
        {
            // Send ACK.
            Send1BitWord( 0 );
        }
        else
        {
            ReadBits( 1 );
        }
    }

    // Dump message on terminal.
    if ( forMe ) UsartPutCStr( PSTR("AvcReadMessage: This message is for me!\r\n") );

    AvcActionID actionID = GetActionID();

    // switch ( actionID ) {
    //   case /* value */:
    // }
    DumpRawMessage( FALSE );

    LedOff();

    return actionID;
}

/*--------------------------------------------------------------------------------------------------

  Name         :  AvcProcessActionID

  Description  :  Perform processing for given action ID.

  Argument(s)  :  actionID (AvcActionID) -> Action ID to process.

  Return value :  (bool) -> TRUE if action performed.

--------------------------------------------------------------------------------------------------*/
bool AvcProcessActionID ( AvcActionID actionID )
{
    // This function relies on the last received message still being loaded in global registers.

    switch ( actionID )
    {
    case ACT_AUX_IN_USE:

        AUX_Enabled = TRUE;
        return FALSE;

    case ACT_TUNER_IN_USE:
    case ACT_TAPE_IN_USE:
//    case ACT_AUDIO_STATUS: This is where we should print interpretted data (Volume, Balance, etc.)
//    case ACT_TUNER_INFO:  Same here
    case ACT_CD_IN_USE:

        DeviceEnabled = actionID;
        AUX_Enabled = FALSE;
        return FALSE;

//    case ACT_NO_CD:

    case ACT_EJECT_CD:

        // Normal CD eject command.
        if ( DeviceEnabled == ACT_CD_IN_USE ) return FALSE;

        if ( AUX_Enabled )
        {
            LoadDataInGlogalRegisters ( &CmdDisableAux );
            AUX_Enabled = FALSE;
        }
        else
        {
            LoadDataInGlogalRegisters ( &CmdEnableAux );
            AUX_Enabled = TRUE;
        }

        return SendMessage();
        break;

    default:

        // No success!
        UsartPutCStr( PSTR("AvcProcessActionID: Unknown action ID!\r\n") );
        //POssibly dumpmsgbuffer here for sebuggimng
        return FALSE;
    }

    // Nothing to do!
    return FALSE;
}

/*--------------------------------------------------------------------------------------------------

  Name         :  LoadDataInGlogalRegisters

  Description  :  Loads message data in global registers for given mesage ID.

  Argument(s)  :  msg (AvcOutMessage *) -> Message to load.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
void LoadDataInGlogalRegisters ( AvcOutMessage * msg )
{
    Description = msg->Description;

    Broadcast = pgm_read_byte_near( &msg->Mode );

    MasterAddress = MY_ADDRESS;

    if ( Broadcast == MSG_BCAST )
        SlaveAddress = BROADCAST_ADDRESS;
    else
        SlaveAddress = HU_ADDRESS;

    DataSize = pgm_read_byte_near( &msg->DataSize );

    for ( byte i = 0; i < DataSize; i++ )
    {
        Data[i] = pgm_read_byte_near( &msg->Data[i] );
    }
}

/*--------------------------------------------------------------------------------------------------

  Name         :  GetActionID

  Description  :  Use the last received message to determine the corresponding action ID.

  Argument(s)  :  None.

  Return value :  (AvcActionID) -> Action ID corresponding to current message.

--------------------------------------------------------------------------------------------------*/
AvcActionID GetActionID ( void )
{
    Description = PSTR("Unknown message!");

    // Iterate through all HU messages in table.
    for ( byte msg = 0; msg < MessageTableSize; msg++ )
    {
        bool found = TRUE;

        // Identify current message from it's payload data.
        for ( byte i = 0; i < pgm_read_byte_near( &MessageTable[msg].DataSize ); i++ )
        {
            if ( Data[i] != pgm_read_byte_near( &MessageTable[msg].Data[i] ) )
            {
                found = FALSE;
                break;
            }
        }

        if ( found )
        {
            Description = MessageTable[msg].Description;

            // Fetch action corresponding to the message.
            AvcActionID actionID = pgm_read_byte_near( &MessageTable[msg].ActionID );

            return actionID;
        }
    }

    return ACT_NONE;
}

/*--------------------------------------------------------------------------------------------------

  Name         :  Send12BitWord

  Description  :  Writes a 12 bit word on the AVC LAN bus.

  Argument(s)  :  data (word) -> Data to write.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
void Send12BitWord ( word data )
{
    ParityBit = 0;

    // Most significant bit out first.
    for ( char nbBits = 0; nbBits < 12; nbBits++ )
    {
        // Reset timer to measure bit length.
        TCNT0 = 0;

        // Drive output to signal high.
        DDRD |= _BV(PD2) | _BV(PD3);

        if ( data & 0x0800 )
        {
            // Adjust parity.
            ParityBit = ! ParityBit;

            while ( TCNT0 < BIT_1_HOLD_ON_LENGTH );
        }
        else
        {
            while ( TCNT0 < BIT_0_HOLD_ON_LENGTH );
        }

        // Release output.
        DDRD &= ~( _BV(PD2) | _BV(PD3) );

        // Hold output low until end of bit.
        while ( TCNT0 < NORMAL_BIT_LENGTH );

        // Fetch next bit.
        data <<= 1;
    }
}

/*--------------------------------------------------------------------------------------------------

  Name         :  Send8BitWord

  Description  :  Writes an 8 bit word on the AVC LAN bus.

  Argument(s)  :  data (byte) -> Data to write.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
void Send8BitWord ( byte data )
{
    ParityBit = 0;

    // Most significant bit out first.
    for ( char nbBits = 0; nbBits < 8; nbBits++ )
    {
        // Reset timer to measure bit length.
        TCNT0 = 0;

        // Drive output to signal high.
        DDRD |= _BV(PD2) | _BV(PD3);

        if ( data & 0x80 )
        {
            // Adjust parity.
            ParityBit = ! ParityBit;

            while ( TCNT0 < BIT_1_HOLD_ON_LENGTH );
        }
        else
        {
            while ( TCNT0 < BIT_0_HOLD_ON_LENGTH );
        }

        // Release output.
        DDRD &= ~( _BV(PD2) | _BV(PD3) );

        // Hold output low until end of bit.
        while ( TCNT0 < NORMAL_BIT_LENGTH );

        // Fetch next bit.
        data <<= 1;
    }
}

/*--------------------------------------------------------------------------------------------------

  Name         :  Send4BitWord

  Description  :  Writes a 4 bit word on the AVC LAN bus.

  Argument(s)  :  data (byte) -> Data to write.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
void Send4BitWord ( byte data )
{
    ParityBit = 0;

    // Most significant bit out first.
    for ( char nbBits = 0; nbBits < 4; nbBits++ )
    {
        // Reset timer to measure bit length.
        TCNT0 = 0;

        // Drive output to signal high.
        DDRD |= _BV(PD2) | _BV(PD3);

        if ( data & 0x8 )
        {
            // Adjust parity.
            ParityBit = ! ParityBit;

            while ( TCNT0 < BIT_1_HOLD_ON_LENGTH );
        }
        else
        {
            while ( TCNT0 < BIT_0_HOLD_ON_LENGTH );
        }

        // Release output.
        DDRD &= ~( _BV(PD2) | _BV(PD3) );

        // Hold output low until end of bit.
        while ( TCNT0 < NORMAL_BIT_LENGTH );

        // Fetch next bit.
        data <<= 1;
    }
}

/*--------------------------------------------------------------------------------------------------

  Name         :  Send1BitWord

  Description  :  Writes a 1 bit word on the AVC LAN bus.

  Argument(s)  :  data (bool) -> Data to write.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
void Send1BitWord ( bool data )
{
    // Reset timer to measure bit length.
    TCNT0 = 0;

    // Drive output to signal high.
    DDRD |= _BV(PD2) | _BV(PD3);

    if ( data )
    {
        while ( TCNT0 < BIT_1_HOLD_ON_LENGTH );
    }
    else
    {
        while ( TCNT0 < BIT_0_HOLD_ON_LENGTH );
    }

    // Release output.
    DDRD &= ~( _BV(PD2) | _BV(PD3) );

    // Pulse level low duration until 40 us.
    while ( TCNT0 <  NORMAL_BIT_LENGTH );
}

/*--------------------------------------------------------------------------------------------------

  Name         :  SendStartBit

  Description  :  Writes a start bit on the AVC LAN bus.

  Argument(s)  :  None.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
void SendStartBit ( void )
{
    // Reset timer to measure bit length.
    TCNT0 = 0;

    // Drive output to signal high.
    DDRD |= _BV(PD2) | _BV(PD3);

    // Pulse level high duration.
    while ( TCNT0 < START_BIT_HOLD_ON_LENGTH );

    // Release output.
    DDRD &= ~( _BV(PD2) | _BV(PD3) );

    // Pulse level low duration until ~185 us.
    while ( TCNT0 < START_BIT_LENGTH );
}

/*--------------------------------------------------------------------------------------------------

  Name         :  ReadBits

  Description  :  Reads specified number of bits from the AVC LAN bus.

  Argument(s)  :  nbBits (byte) -> Number of bits to read.

  Return value :  (word) -> Data value read.

                       |<---- Bit '0' ---->|<---- Bit '1' ---->|
     Physical '1'      ,---------------,   ,---------,         ,---------
                       ^               |   ^         |         ^
     Physical '0' -----'               '---'         '---------'--------- Idle low
                       |---- 32 us ----| 7 |- 20 us -|- 19 us -|

--------------------------------------------------------------------------------------------------*/
word ReadBits ( byte nbBits )
{
    word data = 0;

    ParityBit = 0;

    while ( nbBits-- > 0 )
    {
        // Insert new bit
        data <<= 1;

        // Wait until rising edge of new bit.
        while ( INPUT_IS_CLEAR )
        {
            // Reset watchdog.
            wdt_reset();
        }

        // Reset timer to measure bit length.
        TCNT0 = 0;

        // Wait until falling edge.
        while ( INPUT_IS_SET );

        // Compare half way between a '1' (20 us) and a '0' (32 us ): 32 - (32 - 20) /2 = 26 us
        if ( TCNT0 < BIT_0_HOLD_ON_LENGTH - (BIT_0_HOLD_ON_LENGTH - BIT_1_HOLD_ON_LENGTH) / 2 )
        {
            // Set new bit.
            data |= 0x0001;

            // Adjust parity.
            ParityBit = ! ParityBit;
        }
    }

    return data;
}

/*--------------------------------------------------------------------------------------------------

  Name         :  SendMessage

  Description  :  Sends the message in global registers on the AVC LAN bus.

  Argument(s)  :  None.

  Return value :  (bool) -> TRUE if successful else FALSE.

--------------------------------------------------------------------------------------------------*/
bool SendMessage ( void )
{
    while ( ! IsAvcBusFree() );

    // At this point we know the bus is available.

    LedOn();

    // Send start bit.
    SendStartBit();

    // Broadcast bit.
    Send1BitWord( Broadcast );

    // Master address = me.
    Send12BitWord( MasterAddress );
    Send1BitWord( ParityBit );

    // Slave address = head unit (HU).
    Send12BitWord( SlaveAddress );
    Send1BitWord( ParityBit );

    if ( ! HandleAcknowledge() )
    {
        DumpRawMessage( TRUE );
        UsartPutStr( (char*)"SendMessage: No Ack @ Slave address\r\n" );
        return FALSE;
    }

    // Control flag + parity.
    Send4BitWord( Control );
    Send1BitWord( ParityBit );

    if ( ! HandleAcknowledge() )
    {
        DumpRawMessage( TRUE );
        UsartPutStr( (char*)"SendMessage: No Ack @ Control\r\n" );
        return FALSE;
    }

    // Data length + parity.
    Send8BitWord( DataSize );
    Send1BitWord( ParityBit );

    if ( ! HandleAcknowledge() )
    {
        DumpRawMessage( TRUE );
        UsartPutStr( (char*)"SendMessage: No Ack @ DataSize\r\n" );
        return FALSE;
    }

    for ( byte i = 0; i < DataSize; i++ )
    {
        Send8BitWord( Data[i] );
        Send1BitWord( ParityBit );

        if ( ! HandleAcknowledge() )
        {
            DumpRawMessage( TRUE );
            sprintf( UsartMsgBuffer, "SendMessage: No Ack @ Data[%d]\r\n", i );
            UsartPutStr( UsartMsgBuffer );
            return FALSE;
        }
    }

    DumpRawMessage( TRUE );

    LedOff();

    return TRUE;
}

/*--------------------------------------------------------------------------------------------------

  Name         :  ReadAcknowledge

  Description  :  Reads the acknowledge bit the AVC LAN bus.

  Argument(s)  :  None.

  Return value :  (bool) -> TRUE if ack detected else FALSE.

--------------------------------------------------------------------------------------------------*/
inline bool ReadAcknowledge ( void )
{
    // The acknowledge pattern is very tricky: the sender shall drive the bus for the equivalent
    // of a bit '1' (20 us) then release the bus and listen. At this point the target shall have
    // taken over the bus maintaining the pulse until the equivalent of a bit '0' (32 us) is formed.

    // Reset timer to measure bit length.
    TCNT0 = 0;

    // Drive output to signal high.
    DDRD |= _BV(PD2) | _BV(PD3);

    // Generate bit '0'.
    while ( TCNT0 < BIT_1_HOLD_ON_LENGTH );

    // Release output.
    DDRD &= ~( _BV(PD2) | _BV(PD3) );

    // Measure final resulting bit.
    while ( INPUT_IS_SET );

    // Sample half-way through bit '0' (26 us) to detect whether the target is acknowledging.
    if ( TCNT0 > BIT_0_HOLD_ON_LENGTH - (BIT_0_HOLD_ON_LENGTH - BIT_1_HOLD_ON_LENGTH) / 2 )
    {
        // Slave is acknowledging (ack = 0). Wait until end of ack bit.
        while ( INPUT_IS_SET );
        return TRUE;
    }

    // No sign of life on the bus.
    return FALSE;
}

/*--------------------------------------------------------------------------------------------------

  Name         :  HandleAcknowledge

  Description  :  Sends ack bit if I am broadcasting otherwise wait and return received ack bit.

  Argument(s)  :  None.

  Return value :  (bool) -> FALSE if ack bit not detected.

--------------------------------------------------------------------------------------------------*/
bool HandleAcknowledge ( void )
{
    if ( Broadcast == MSG_BCAST )
    {
        // Acknowledge.
        Send1BitWord( 0 );
        return TRUE;
    }

    // Return acknowledge bit.
    return ReadAcknowledge();
}

/*--------------------------------------------------------------------------------------------------

  Name         :  IsAvcBusFree

  Description  :  Determine whether the bus is free (no tx/rx).

  Argument(s)  :  None.

  Return value :  (bool) -> TRUE is bus is free.

--------------------------------------------------------------------------------------------------*/
bool IsAvcBusFree ( void )
{
    // Reset timer.
    TCNT0 = 0;

    while ( INPUT_IS_CLEAR )
    {
        // We assume the bus is free if anything happens for the length of 1 bit.
        if ( TCNT0 > NORMAL_BIT_LENGTH )
        {
            return TRUE;
        }
    }

    return FALSE;
}

/*--------------------------------------------------------------------------------------------------

  Name         :  DumpRawMessage

  Description  :  Dumps raw content of message registers on the terminal.

  Argument(s)  :  incoming (bool) -> TRUE means incoming data, FALSE means outgoing.

  Return value :  None.

--------------------------------------------------------------------------------------------------*/
void DumpRawMessage ( bool incoming )
{
    // Dump message on terminal.

    if ( incoming )
        UsartPutCStr( PSTR("\r\nAUX Enabler <<--- HU\r\n") );
    else
        UsartPutCStr( PSTR("\r\nAUX Enabler --->> HU\r\n") );

    UsartPutCStr( PSTR("   Description:    ") );
    UsartPutCStr( Description );
    UsartPutCStr( PSTR("\r\n") );

    sprintf( UsartMsgBuffer, "   Broadcast:      %d \r\n", Broadcast );
    UsartPutStr( UsartMsgBuffer );

    sprintf( UsartMsgBuffer, "   Master address: 0x%X \r\n", MasterAddress );
    UsartPutStr( UsartMsgBuffer );

    sprintf( UsartMsgBuffer, "   Slave address:  0x%X \r\n", SlaveAddress );
    UsartPutStr( UsartMsgBuffer );

    sprintf( UsartMsgBuffer, "   Control:        0x%X \r\n", Control );
    UsartPutStr( UsartMsgBuffer );

    sprintf( UsartMsgBuffer, "   Data size:      %d \r\n", DataSize );
    UsartPutStr( UsartMsgBuffer );

    sprintf( UsartMsgBuffer, "   Data:           " );
    UsartPutStr( UsartMsgBuffer );

    for ( byte i = 0; i < DataSize; i++ )
    {
        sprintf( UsartMsgBuffer, "%X ", Data[i] );
        UsartPutStr( UsartMsgBuffer );
    }

    UsartPutStr( (char*)"\r\n-----\r\n" );
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
                                         End of file.
--------------------------------------------------------------------------------------------------*/
