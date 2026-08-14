--[[
    Copyright (C) 2023 Allen Hill <allenofthehills@gmail.com>

    Portions of the following are based on code from libsigrokdecode PR that is
    copyright (C) 2023 Maciej Grela <enki@fsck.pl>

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
--]]

local iebusproto = Proto("iebus", "IEBus protocol")

-- 12-bit IEBus network (bus) addresses. Sourced from:
--  - Marcin/Soft-service
--  - the ADDR_* table in GadgetNutt/AVC-LAN-Module-Builder
--  - lists.py in sigrokproject/libsigrokdecode#106 by @enkiusz
local known_addresses = {
    [0x1F1] = "XM",
    [0x110] = "EMV",
    [0x120] = "AVX",
    [0x128] = "1DIN TV",
    [0x140] = "AVN",
    [0x144] = "G-BOOK",
    [0x160] = "AUDIO H/U",
    [0x17C] = "MONET",
    [0x17D] = "TEL",
    [0x178] = "NAV_W_CONTROLS",
    [0x180] = "AUDIO_ECU", -- @enkiusz labels as Rr_TV (rear TV)
    [0x190] = "AUDIO_HU",
    [0x1A0] = "DVD_P",
    [0x1AC] = "CAMERA-C",
    [0x1B0] = "REAR_TV",
    [0x1B4] = "SINGLE_DIN_NAV",
    [0x1B8] = "DISPLAY_SW",
    [0x1C0] = "REAR_CTRL_SW",
    [0x1C2] = "EURO_GW_ECU", -- @enkiusz labels as TV_TUNER2
    [0x1C4] = "RUSSIA_GW_ECU", -- @enkiusz labels as PANEL
    [0x1C6] = "GW_ECU",
    [0x1C8] = "FM_MULTI_DISPLAY",
    [0x1CC] = "STEERING_SW",
    [0x1D0] = "MULTI_CD_DECODER",
    [0x1D4] = "DISPLAY",
    [0x1D6] = "CLOCK",
    [0x1D8] = "FR_CONTROLLED_SW", -- @enkiusz labels as GW_TRIP (G/W for Trip)
    [0x1DC] = "NAV_REM_CTRL",
    [0x1E0] = "CD_CH_COMMANDER",
    [0x1E4] = "CONSOLIDATED_SW",
    [0x1E8] = "MD_CH_COMMANDER",
    [0x1EC] = "BODY_COMPUTER",
    [0x1F0] = "AMP_RADIO_TUNER",
    [0x1F2] = "XM_RADIO_TUNER", -- @enkiusz labels as SIRIUS
    [0x1F4] = "RSA",
    [0x1F6] = "RSE_M",
    [0x1FF] = "AUDIO_BROADCAST",
    [0x200] = "NAV_ECU",
    [0x210] = "ATIS",
    [0x220] = "VICS",
    [0x230] = "TV_TUNER",
    [0x240] = "HW_CD_CH",
    [0x250] = "HW_DVD_CH",
    [0x260] = "TEL_INFO_ECU",
    [0x280] = "CAMERA_CTRLR",
    [0x300] = "RADIO",
    [0x320] = "CASSETTE",
    [0x330] = "CASSETTE_NO_CH",
    [0x340] = "CD_P",
    [0x360] = "1DIN_CD_CH",
    [0x380] = "MD_P",
    [0x3A0] = "MD_CH",
    [0x3C0] = "DAT",
    [0x3E0] = "DCC",
    [0x3F8] = "TEL_ECU",
    [0x400] = "EQUALIZER",
    [0x440] = "DSP",
    [0x480] = "HW_AMP",
    [0x500] = "GPS_RECEIVER",
    [0x510] = "ATIS_DECODER",
    [0x520] = "FM_MULTI_DECODER",
    [0x528] = "RADIO_WAVE_BEACON",
    [0x52C] = "OPTICAL_BEACON",
    [0x530] = "ETC",
    [0x540] = "CD_CH",
    [0x560] = "MD_CH_2",
    [0x580] = "CDROM_CH",
    [0x5A0] = "MDROM_CH",
    [0x5C0] = "TEL_INFO",
    [0x5C8] = "MAYDAY",
    [0x600] = "AC_ECU",
    [0x680] = "BODY_ECU",
    [0xD19] = "TURN_SIGNAL",
    [0xFFF] = "BROADCAST",
}

local f_unicast = ProtoField.bool("iebus.unicast", "Unicast", base.NONE, { [1] = "true", [2] = "false" })
local f_controller_addr = ProtoField.uint16("iebus.controller", "Controller address", base.HEX, known_addresses, 0x0FFF)
local f_peripheral_addr = ProtoField.uint16("iebus.peripheral", "Peripheral address", base.HEX, known_addresses, 0x0FFF)
local f_control = ProtoField.uint8("iebus.control", "Control field", base.HEX)
local f_length = ProtoField.uint8("iebus.length", "Data length", base.DEC)
local f_data = ProtoField.bytes("iebus.data", "Frame data", base.SPACE)

iebusproto.fields = {
    f_unicast,
    f_controller_addr,
    f_peripheral_addr,
    f_control,
    f_length,
    f_data
}

local controlbits = {
    [0x0] = "READ PERIPH STATUS (SSR)",
    [0x1] = "UNDEFINED",
    [0x2] = "UNDEFINED",
    [0x3] = "READ & LOCK DATA",
    [0x4] = "READ LOCK ADDR (lo8)",
    [0x5] = "READ LOCK ADDR (hi4)",
    [0x6] = "READ & UNLOCK PERIPH STATUS (SSR)",
    [0x7] = "READ DATA",
    [0x8] = "UNDEFINED",
    [0x9] = "UNDEFINED",
    [0xA] = "WRITE & LOCK CMD",
    [0xB] = "WRITE & LOCK DATA",
    [0xC] = "UNDEFINED",
    [0xD] = "UNDEFINED",
    [0xE] = "WRITE CMD",
    [0xF] = "WRITE DATA"
}

local periphstatusbits = {
    [0] = "TX BUFFER EMPTY",
    [1] = "RX BUFFER EMPTY",
    [2] = "UNIT LOCKED",
    [4] = "TX ENABLED",
    [6] = "IEBUS MODE 1",
    [7] = "IEBUS MODE 2",
}

-- Expert info for unknown packet types, set as group PI_UNDECODED, severity PI_NOTE or PI_WARN
-- Expert info for LAN Check [0x3, SW_ID, 0x01, 0x20] as severity PI_CHAT

function iebusproto.dissector(buffer, pinfo, tree)
    length = buffer:len()
    if length == 0 then return end

    pinfo.cols.protocol = iebusproto.name

    local subtree = tree:add(iebusproto, buffer(), "IEBus frame")

    subtree:add(f_unicast, buffer(0,1))
    subtree:add(f_controller_addr, buffer(1,2))
    subtree:add(f_peripheral_addr, buffer(3,2))
    pinfo.cols.src = buffer(1,2):bytes():tohex():sub(2,-1)
    pinfo.cols.dst = buffer(3,2):bytes():tohex():sub(2,-1)

    local control_item, control = subtree:add_packet_field(f_control, buffer(5,1), ENC_LITTLE_ENDIAN)
    -- print("control val" .. control)
    control_item:append_text(" (" .. controlbits[buffer(5,1):uint()] .. ")")

    local _, datalen = subtree:add_packet_field(f_length, buffer(6,1), ENC_LITTLE_ENDIAN)

    local data = subtree:add(iebusproto, buffer(), "IEBus message")
    data:add(f_data, buffer(7, datalen))

end

local udlt = DissectorTable.get("wtap_encap")
udlt:add(wtap.USER15, iebusproto)

local avclanproto = Proto("avclan", "AVCLAN protocol")

-- Build a reverse {value = key} lookup from a {key = value} table. Lets us keep
-- a single source-of-truth value->name table (also used as the ProtoField
-- valuestring) and derive the name->value lookup instead of hand-maintaining two.
local function invert(t)
    local r = {}
    for k, v in pairs(t) do
        r[v] = k
    end
    return r
end

-- Decode a BCD-encoded byte to a decimal number. Returns nil when either nibble
-- is out of range (> 9), which means the byte isn't valid BCD -- e.g. a 0xff
-- "not present" sentinel, or a value the device actually encodes in plain
-- binary. Callers decide how to present the invalid case.
local function bcd2dec(b)
    local hi = bit.rshift(b, 4)
    local lo = bit.band(b, 0x0F)
    if hi > 9 or lo > 9 then
        return nil
    end
    return hi * 10 + lo
end

-- Format a BCD-encoded mm:ss pair, falling back to raw hex when a byte isn't
-- valid BCD (e.g. a 0xff sentinel for "time unknown"). `negative` (the CD
-- NEGATIVE flag) only controls the sign, so it applies to the raw fallback too.
local function bcd_time(min_b, sec_b, negative)
    local sign = negative and "-" or ""
    local m = bcd2dec(min_b)
    local s = bcd2dec(sec_b)
    if m == nil or s == nil then
        return string.format("%s%02x:%02x (raw)", sign, min_b, sec_b)
    end
    return string.format("%s%02d:%02d", sign, m, s)
end

local known_devices = {
    [0x00] = "LAN",
    [0x01] = "COMM_CTRL",
    -- [0x02] = "COM_EXT", -- @GadgetNutt
    [0x11] = "COMMUNICATION v1", -- @GadgetNutt reads 0x11 as COM_MASTER
    [0x12] = "COMMUNICATION v2",
    [0x21] = "SW_AUDIO", -- @GadgetNutt name
    [0x23] = "SW_NAME", -- @GadgetNutt reads 0x23 as SW_SHIFT
    [0x24] = "SW_CONVERTING",
    [0x25] = "CMD_SW", -- @GadgetNutt reads 0x25 as SW
    [0x31] = "STATUS",
    -- [0x32] corroborated by @GadgetNutt (INFO_DISPLAY2); sends status report f1 to STATUS (32 31 f1 00 00) [CQ-TS7471LC_bootup]
    [0x32] = "INFO_DISPLAY2",
    [0x28] = "BEEP_HU",
    [0x29] = "BEEP_SPEAKERS",
    [0x34] = "FRONT_PSNG_MONITOR",
    -- [0x40] = "TV_TUNER", -- @GadgetNutt
    [0x43] = "CD_CHANGER2",
    -- [0x50] = "CD_50", -- @GadgetNutt (CD variants, unconfirmed)
    -- [0x52] = "CD_52",
    [0x55] = "BLUETOOTH_TEL",
    [0x56] = "INFO_DRAWING",
    [0x58] = "NAV_ECU", -- @GadgetNutt reads 0x58 as NAV_GPS
    -- [0x5A] = "FM_MULTIPLEX_VICS", 
    -- [0x5B] = "BEACON", -- @GadgetNutt
    [0x5C] = "CAMERA",
    [0x5D] = "CLIMATE_DRAWING",
    [0x5E] = "AUDIO_DRAWING",
    [0x5F] = "TRIP_INFO_DRAWING",
    [0x60] = "TUNER",
    [0x61] = "TAPE_DECK",
    [0x62] = "CD_SINGLE",
    [0x63] = "CD_CHANGER",
    -- [0x64] = "MD", -- @GadgetNutt (MiniDisc)
    -- [0x65] = "MD_CH",
    [0x74] = "AUDIO_AMP",
    [0x80] = "GPS",
    -- [0x82] = "FM_MULTIPLEX_DATA",
    -- [0x83] = "OPTICAL_BEACON",
    -- [0x84] = "RADIO_WAVE_BEACON", -- @GadgetNutt
    [0x85] = "VOICE_CTRL",
    -- [0x9A] = "FM_MULTIPLEX_TUNER",-- @GadgetNutt (0xA4 also seen in the A4 01 DB heartbeat)
    -- 0xc0 corroborated by @GadgetNutt (XM_TUNER, cf. ADDR XM_RADIO_TUNER); exchanges 0xef/0xff with CMD_SW, sends 0xfe to STATUS [douglasheld2-log.pcap]
    [0xC0] = "XM_TUNER",
    [0xE0] = "CLIMATE_CTRL_DEV",
    [0xE5] = "TRIP_INFO",
}
local known_devices_names = invert(known_devices)

local f_from_device = ProtoField.uint8("avclan.from_device", "From device", base.HEX, known_devices)
local f_to_device = ProtoField.uint8("avclan.to_device", "To device", base.HEX, known_devices)
local f_active_device = ProtoField.uint8("avclan.active_device", "Active device", base.HEX, known_devices)

-- Action conventions:
--  - REQ/RESP pairs differ by 0x10, where the REQ is the lesser
--  - Opposite pairs (e.g. disc up/down, enable/disable, etc.) differ by 0x01, where the "enable" like action is the lesser
--  - Separate pairs are often (but not always) separated by 0x01
local known_actions = {
    -- LAN related
    [0x01] = "LAN_INIT",
    [0x58] = "LAN_INIT_COMPLETE", -- Probably not fully correct label pair (based on values), but seem related functionally/sequentially
    -- [0x5f] = "??", -- COMM_CTRL self-broadcast (01 01 5f 00), frequent in douglasheld2-log.pcap [but abnormal traffic]
    [0x00] = "LIST_FUNCTIONS_REQ",
    [0x10] = "LIST_FUNCTIONS_RESP",
    -- -- [0x02]/[0x12] REQ/RESP pair, COMM_v2 <-> COMM_CTRL. The 0x12 resp carries a
    -- -- device list, so likely a richer enumeration than 0x00/0x10. (0x12 collides
    -- -- with the COMMUNICATION v2 device id.) [CQ-TS7471LC_bootup]
    -- -- @GadgetNutt corroborates the enumeration reading: 0x02=DEVICES_RESPONSE,
    -- -- 0x12=DEVICES_REQUEST ("devices I want to hear"), 0x13=DEVICES_BROADCAST.
    -- [0x02] = "??_REQ",  -- 12 01 02 [19 00 16 06 19 0d]
    -- [0x12] = "??_RESP", -- 01 12 12 60 63 43 64 65 40 32 5e ...
    -- [0x13] = "DEVICES_BROADCAST", -- @GadgetNutt: peripheral broadcasts its logical-device list (b <me> 1FF 01 11 13 ...)
    [0x08] = "LANCHECK_END_REQ",
    [0x18] = "LANCHECK_END_RESP",
    [0x0a] = "LANCHECK_SCAN_REQ",
    [0x1a] = "LANCHECK_SCAN_RESP",
    [0x0c] = "LANCHECK_REQ",
    [0x1c] = "LANCHECK_RESP",
    -- [0x0d] = "LANCHECK_??_REQ", -- Observed in @marcin's code; no captured examples
    -- [0x1d] = "LANCHECK_??_RESP",

    [0x20] = "PING_REQ",
    -- [0x21] = "??" -- Observed as [00 11 00 21] incorrect 0xf0/INITIAL_REPORT_RESP message (double leading zeros instead of single unicast leading zero)
    [0x30] = "PING_RESP",

    [0x42] = "ENABLE_FUNCTION_REQ",
    [0x52] = "ENABLE_FUNCTION_RESP",
    [0x43] = "DISABLE_FUNCTION_REQ",
    [0x53] = "DISABLE_FUNCTION_RESP",

    [0x45] = "ADVERTISE_FUNCTION",
    [0x46] = "GENERAL_QUERY",

    -- Physical actions
    [0x50] = "INSERTION",
    [0x51] = "EJECTION",
    [0x59] = "BACKLIGHT_ADJUST",
    [0x60] = "BEEP",
    [0x78] = "SCREEN_PRESS",
    [0x80] = "EJECT",
    -- [0x8e] = "??", -- COMM_v2->CD (12 62 8e, no payload) [CQ-TS7471LC_bootup]
    [0x90] = "DISC_UP",
    [0x91] = "DISC_DOWN",
    [0x94] = "TRACK_SEEK_UP",
    [0x95] = "TRACK_SEEK_DOWN",
    [0x98] = "TRACK_FAST_FORWARD",
    [0x99] = "TRACK_REWIND",
    [0x9c] = "PWRVOL_KNOB_RIGHTHAND_TURN",
    [0x9d] = "PWRVOL_KNOB_LEFTHAND_TURN",
    [0x9f] = "TAPE_NOT_READY", -- Uncertain; observed by @marcin
    [0xa0] = "CD_ENABLE_REPEAT",
    [0xa1] = "CD_DISABLE_REPEAT",
    [0xa3] = "CD_ENABLE_DISK_REPEAT",
    [0xa4] = "CD_DISABLE_DISK_REPEAT",
    [0xa6] = "CD_ENABLE_SCAN",
    [0xa7] = "CD_DISABLE_SCAN",
    [0xa9] = "CD_ENABLE_DISK_SCAN",
    [0xaa] = "CD_DISABLE_DISK_SCAN",
    [0xb0] = "CD_ENABLE_RANDOM",
    [0xb1] = "CD_DISABLE_RANDOM",
    [0xb3] = "CD_ENABLE_DISK_RANDOM",
    [0xb4] = "CD_DISABLE_DISK_RANDOM",

    -- Info-display / trip-computer functions. @GadgetNutt names these; cluster
    -- around TRIP_INFO/INFO_DISPLAY and the A4 01 DB heartbeat. Unconfirmed here.
    -- [0xb7] = "STATUS_B7",       -- @GadgetNutt
    -- [0xd9] = "INFO_D9",         -- @GadgetNutt (info display, unidentified)
    -- [0xdb] = "AVG_KMH_INFO",    -- @GadgetNutt (average km/h)
    -- [0xdc] = "INFO_DC",         -- @GadgetNutt (unidentified)
    -- [0xdd] = "FUEL_RANGE_INFO", -- @GadgetNutt
    -- [0xde] = "TRIP_TIME_INFO",  -- @GadgetNutt

    -- Peripheral state communication request/response
    [0xe0] = "INITIAL_REPORT_REQ",
    [0xf0] = "INITIAL_REPORT_RESP",
    [0xe2] = "PLAYBACK_REQ",
    [0xf2] = "PLAYBACK_RESP",
    [0xe4] = "LOADING_REQ",
    [0xf4] = "LOADING_RESP",
    [0xed] = "TRACK_NAME_REQ",
    [0xfd] = "TRACK_NAME_RESP",

    -- Numerically goes here, but message length (short) tracks more with a LAN related pair
    -- (Also this was observed early in a capture)
    -- [0xef] = "??_REQ", -- Observed in broadcast(?) message 190 => 1f1 00 25 c0 ef [douglasheld2-log.pcap]
    -- [0xff] = "??_RESP", -- Observed in broadcast(?) message 1f1 => 190 00 c0 25 ff 00 [douglasheld2-log.pcap]

    -- Unprompted peripheral state announcements
    [0xf1] = "PLAYBACK_STATUS",
    [0xf3] = "LOADING_STATUS",
    [0xf9] = "REPORT_TOC",
    -- [0xfe] = "??", -- dev 0xc0->STATUS (c0 31 fe 00), status-announcement-like [douglasheld2-log.pcap]
}
local known_actions_names = invert(known_actions)

local f_action = ProtoField.uint8("avclan.action", "Action", base.HEX, known_actions)
local f_functions = ProtoField.bytes("avclan.functions", "Functions", base.SPACE, "Device functions")

local f_ping_count = ProtoField.uint8("avclan.ping.count", "Ping count")
local f_backlight = ProtoField.uint8("avclan.backlight.brightness", "Backlight brightness", base.HEX)

-- Beep request (AUDIO_DRAWING -> BEEP_SPEAKERS, action 0x60): duration parameter
local f_beep_duration = ProtoField.uint8("avclan.beep.duration", "Beep duration", base.DEC)

-- Touch-screen press (SW -> SW_CONVERTING, action 0x78): x,y position
local f_touch_x = ProtoField.uint8("avclan.touch.x", "Touch X", base.HEX)
local f_touch_y = ProtoField.uint8("avclan.touch.y", "Touch Y", base.HEX)

local f_radio_active = ProtoField.bool("avclan.radio.active", "Radio", base.NONE, {"ON", "OFF"})
local f_radio_status = ProtoField.uint8("avclan.radio.status", "Radio status", base.HEX,
    {
        [0x00] = "OFF",
        [0x01] = "READY",
        [0x06] = "SCAN UP",
        [0x07] = "SCAN DOWN",
        [0x0A] = "AST SEARCH",
        [0x27] = "MANUAL"
    }
)
local f_radio_flags = ProtoField.uint8("avclan.radio.flags", "Radio flags")
local f_radio_flags2 = ProtoField.uint8("avclan.radio.flags2", "Radio flags (byte 2)")
-- Preset/channel number (0 when not on a stored preset).
local f_radio_channel = ProtoField.uint8("avclan.radio.channel", "Preset channel", base.DEC)

-- NOTE: bit 0x04 is read here as Stereo. @enkiusz (TunerFlags) reads the same
-- bit as TP (Traffic Programme). One of these is wrong; unresolved.
local f_radioflag_st = ProtoField.bool("avclan.radio.flags.st", "Stereo", 8, {"Set", "Not set"}, 0x04)
local f_radioflag_ta = ProtoField.bool("avclan.radio.flags.ta", "TA", 8, {"Set", "Not set"}, 0x08)
local f_radioflag_reg = ProtoField.bool("avclan.radio.flags.reg", "REG", 8, {"Set", "Not set"}, 0x10)
local f_radioflag_af = ProtoField.bool("avclan.radio.flags.af", "AF", 8, {"Set", "Not set"}, 0x40)

local f_radio_band = ProtoField.uint8("avclan.radio.band", "Radio band", base.HEX, 
    {[0x8] = "FM", [0xC] = "AM (Long-wave)", [0x0] = "AM (Medium-wave)"}, 0xF0)
local f_radio_bandnumber = ProtoField.int8("avclan.radio.bandnumber", "Radio band number", base.DEC, nil, 0x0F)
local f_radio_freq = ProtoField.uint16("avclan.radio.freq", "Radio frequency")

-- Volume is BCD encoded here. NOTE: @enkiusz treats the volume byte as plain
-- binary (no BCD conversion); the two disagree for values with a nibble > 9.
local f_amp_volume = ProtoField.uint8("avclan.amp.volume", "Volume", base.DEC)

-- Amp bass, mid, treble, fade, and balance are offset-binary (offset-16) encoded
local f_amp_bass = ProtoField.uint8("avclan.amp.bass", "Bass", base.HEX, {
    [0x0B] = "-5", [0x0C] = "-4", [0x0D] = "-3", [0x0E] = "-2", [0x0F] = "-1",
    [0x10] = "0",
    [0x11] = "+1", [0x12] = "+2", [0x13] = "+3", [0x14] = "+4", [0x15] = "+5"
})
local f_amp_mid = ProtoField.uint8("avclan.amp.mid", "Mid", base.HEX, {
    [0x0B] = "-5", [0x0C] = "-4", [0x0D] = "-3", [0x0E] = "-2", [0x0F] = "-1",
    [0x10] = "0",
    [0x11] = "+1", [0x12] = "+2", [0x13] = "+3", [0x14] = "+4", [0x15] = "+5"
})
local f_amp_treble = ProtoField.uint8("avclan.amp.treble", "Treble", base.HEX, {
    [0x0B] = "-5", [0x0C] = "-4", [0x0D] = "-3", [0x0E] = "-2", [0x0F] = "-1",
    [0x10] = "0",
    [0x11] = "+1", [0x12] = "+2", [0x13] = "+3", [0x14] = "+4", [0x15] = "+5"
})
local f_amp_fade = ProtoField.uint8("avclan.amp.fade", "Fade (forward/rear)", base.HEX, {
    [0x09] = "-7", [0x0A] = "-6", [0x0B] = "-5", [0x0C] = "-4", [0x0D] = "-3", [0x0E] = "-2", [0x0F] = "-1",
    [0x10] = "0",
    [0x11] = "+1", [0x12] = "+2", [0x13] = "+3", [0x14] = "+4", [0x15] = "+5", [0x16] = "+6", [0x17] = "+7"
})
local f_amp_balance = ProtoField.uint8("avclan.amp.balance", "Balance (right/left)", base.HEX, {
    [0x09] = "-7", [0x0A] = "-6", [0x0B] = "-5", [0x0C] = "-4", [0x0D] = "-3", [0x0E] = "-2", [0x0F] = "-1",
    [0x10] = "0",
    [0x11] = "+1", [0x12] = "+2", [0x13] = "+3", [0x14] = "+4", [0x15] = "+5", [0x16] = "+6", [0x17] = "+7"
})

-- Amp status flags (from @enkiusz's AudioAmpFlags). Only MUTE (0x04) is
-- identified; the rest are unknown. Note this byte (data_bytes[12]) sits past
-- the end of the 14-byte amp reports in our captures, so it only decodes on
-- longer report variants.
local f_amp_flags = ProtoField.uint8("avclan.amp.flags", "Amplifier flags", base.HEX)
local f_amp_mute = ProtoField.bool("avclan.amp.flags.mute", "MUTE", 8, nil, 0x04)

local f_cd_slots = ProtoField.uint8("avclan.cd.slots", "CD player disc slots")
local f_cd_slot1 = ProtoField.bool("avclan.cd.slot1", "Slot 1", 6, {"Filled", "Empty"}, 0x01)
local f_cd_slot2 = ProtoField.bool("avclan.cd.slot1", "Slot 2", 6, {"Filled", "Empty"}, 0x02)
local f_cd_slot3 = ProtoField.bool("avclan.cd.slot1", "Slot 3", 6, {"Filled", "Empty"}, 0x04)
local f_cd_slot4 = ProtoField.bool("avclan.cd.slot1", "Slot 4", 6, {"Filled", "Empty"}, 0x08)
local f_cd_slot5 = ProtoField.bool("avclan.cd.slot1", "Slot 5", 6, {"Filled", "Empty"}, 0x10)
local f_cd_slot6 = ProtoField.bool("avclan.cd.slot1", "Slot 6", 6, {"Filled", "Empty"}, 0x20)

local f_cd_state = ProtoField.uint8("avclan.cd.state", "CD player state")
local f_cd_open = ProtoField.bool("avclan.cd.state.open", "OPEN", 8, nil, 0x01)
local f_cd_err1 = ProtoField.bool("avclan.cd.state.err1", "ERR1", 8, nil, 0x02)
local f_cd_seeking = ProtoField.bool("avclan.cd.state.seeking", "SEEKING", 8, nil, 0x08)
local f_cd_playback = ProtoField.bool("avclan.cd.state.playback", "PLAYBACK", 8, nil, 0x10)
local f_cd_seeking_track = ProtoField.bool("avclan.cd.state.seeking_track", "SEEKING_TRACK", 8, nil, 0x20)
local f_cd_loading = ProtoField.bool("avclan.cd.state.loading", "LOADING", 8, nil, 0x80)

local f_cd_disc = ProtoField.uint8("avclan.cd.disc", "Current disc", base.DEC)
local f_cd_track = ProtoField.uint8("avclan.cd.track", "Track number", base.HEX)
local f_cd_track_count = ProtoField.uint8("avclan.cd.track_count", "Track count", base.DEC)
local f_cd_title = ProtoField.string("avclan.cd.title", "Track/disc title")
local f_cd_min = ProtoField.uint8("avclan.cd.mins", "CD track play time, minutes", base.HEX)
local f_cd_sec = ProtoField.uint8("avclan.cd.secs", "CD track play time, seconds", base.HEX)

local f_cd_flags = ProtoField.uint8("avclan.cd.flags", "CD player flags")
local f_cd_flag_disk_random = ProtoField.bool("avclan.cd.flags.disk_random", "DISK_RANDOM", 8, nil, 0x02)
local f_cd_flag_random = ProtoField.bool("avclan.cd.flags.random", "RANDOM", 8, nil, 0x04)
local f_cd_flag_disk_repeat = ProtoField.bool("avclan.cd.flags.disk_repeat", "DISK_REPEAT", 8, nil, 0x08)
local f_cd_flag_repeat = ProtoField.bool("avclan.cd.flags.repeat", "REPEAT", 8, nil, 0x10)
local f_cd_flag_disk_scan = ProtoField.bool("avclan.cd.flags.disk_scan", "DISK_SCAN", 8, nil, 0x20)
local f_cd_flag_scan = ProtoField.bool("avclan.cd.flags.scan", "SCAN", 8, nil, 0x40)

-- Second CD flags byte (last byte of the playback status frame). Resting value
-- is 0x80; 0x40 (NEGATIVE) makes the reported play time count below zero, as
-- seen when rewinding past the start of a track: ..., 00:01, 00:00, then the
-- byte flips to 0xc0 and the time climbs again as -00:01, -00:02, ...
-- [msgdumps/rewind-negative.txt].
local f_cd_flags2 = ProtoField.uint8("avclan.cd.flags2", "CD player flags (byte 2)")
local f_cd_flag_negative = ProtoField.bool("avclan.cd.flags2.negative", "NEGATIVE", 8, nil, 0x40)
local f_cd_flag2_unknown7 = ProtoField.bool("avclan.cd.flags2.unknown7", "UNKNOWN7", 8, nil, 0x80)

local f_tape_present = ProtoField.uint8("avclan.tape.present", "Tape deck slot", base.HEX, {[0x01] = "FILLED", [0x00] = "EMPTY"})
local f_tape_state = ProtoField.uint8("avclan.tape.state", "Tape deck state")
local f_tape_seeking_rev = ProtoField.bool("avclan.tape.state.seeking_rev", "SEEKING_REVERSE", 8, nil, 0x01)
local f_tape_err1 = ProtoField.bool("avclan.tape.state.err1", "ERR1", 8, nil, 0x02)
local f_tape_playback = ProtoField.bool("avclan.tape.state.playback", "PLAYBACK", 8, nil, 0x04)
local f_tape_seeking = ProtoField.bool("avclan.tape.state.seeking", "SEEKING", 8, nil, 0x08)
local f_tape_state1 = ProtoField.bool("avclan.tape.state.unknown1", "UNKOWN1", 8, nil, 0x10)
local f_tape_random = ProtoField.bool("avclan.tape.state.random", "RANDOM", 8, nil, 0x80)

local f_tape_flags = ProtoField.uint16("avclan.tape.flags", "Tape deck flags")
local f_tape_stereo = ProtoField.bool("avclan.tape.flags.stereo", "STEREO", 16, nil, 0x0004)
local f_tape_dolby = ProtoField.bool("avclan.tape.flags.dolby", "DOLBY", 16, nil, 0x0002)
local f_tape_flag1 = ProtoField.bool("avclan.tape.flags.flag1", "UNKNOWN1", 16, nil, 0x0100)
local f_tape_flag2 = ProtoField.bool("avclan.tape.flags.flag2", "UNKNOWN2", 16, nil, 0x1000)
local f_tape_flag3 = ProtoField.bool("avclan.tape.flags.flag3", "UNKNOWN3", 16, nil, 0x2000)
local f_tape_flag4 = ProtoField.bool("avclan.tape.flags.flag4", "UNKNOWN4", 16, nil, 0x8000)

avclanproto.fields = {
    f_from_device,
    f_to_device,
    f_active_device,
    f_action,
    f_functions,
    f_ping_count,
    f_backlight,
    f_beep_duration,
    f_touch_x,
    f_touch_y,
    f_radio_active,
    f_radio_status,
    f_radio_flags,
    f_radio_flags2,
    f_radio_band,
    f_radio_bandnumber,
    f_radio_freq,
    f_radio_channel,
    f_radioflag_af,
    f_radioflag_reg,
    f_radioflag_st,
    f_radioflag_ta,
    f_amp_volume,
    f_amp_bass,
    f_amp_mid,
    f_amp_treble,
    f_amp_fade,
    f_amp_balance,
    f_amp_flags,
    f_amp_mute,
    f_cd_slots,
    f_cd_slot1,
    f_cd_slot2,
    f_cd_slot3,
    f_cd_slot4,
    f_cd_slot5,
    f_cd_slot6,
    f_cd_state,
    f_cd_open,
    f_cd_err1,
    f_cd_seeking,
    f_cd_playback,
    f_cd_seeking_track,
    f_cd_loading,
    f_cd_disc,
    f_cd_track,
    f_cd_track_count,
    f_cd_title,
    f_cd_min,
    f_cd_sec,
    f_cd_flags,
    f_cd_flag_disk_random,
    f_cd_flag_random,
    f_cd_flag_disk_repeat,
    f_cd_flag_repeat,
    f_cd_flag_disk_scan,
    f_cd_flag_scan,
    f_cd_flags2,
    f_cd_flag_negative,
    f_cd_flag2_unknown7,
    f_tape_present,
    f_tape_state,
    f_tape_seeking_rev,
    f_tape_err1,
    f_tape_playback,
    f_tape_seeking,
    f_tape_state1,
    f_tape_random,
    f_tape_flags,
    f_tape_stereo,
    f_tape_dolby,
    f_tape_flag1,
    f_tape_flag2,
    f_tape_flag3,
    f_tape_flag4,
}

local pe_unhandled_msg = ProtoExpert.new("avclan.expert", "Message not decoded",
    expert.group.UNDECODED, expert.severity.WARN)
-- Parsed-but-unrecognized header values: the frame structure is understood, only
-- the device/action byte is absent from our lookup tables. NOTE severity (vs the
-- WARN above) keeps these distinct from genuinely undecodable payloads, and the
-- dedicated abbreviations make each independently filterable in Wireshark.
local pe_unknown_device = ProtoExpert.new("avclan.unknown_device.expert", "Unknown device address",
    expert.group.UNDECODED, expert.severity.NOTE)
local pe_unknown_action = ProtoExpert.new("avclan.unknown_action.expert", "Unknown action",
    expert.group.UNDECODED, expert.severity.NOTE)
-- local pe_lan_check = ProtoExpert.new("avclan.lan.expert", "")
local pe_ping_req = ProtoExpert.new("avclan.ping_req.expert", "Ping request",
    expert.group.SEQUENCE, expert.severity.CHAT)
local pe_ping_resp = ProtoExpert.new("avclan.ping_resp.expert", "Ping response",
    expert.group.SEQUENCE, expert.severity.CHAT)

avclanproto.experts = {
    pe_unhandled_msg,
    pe_unknown_device,
    pe_unknown_action,
    pe_ping_req,
    pe_ping_resp,
}

local field_from_device = Field.new("avclan.from_device")
local field_to_device = Field.new("avclan.to_device")
local field_action = Field.new("avclan.action")
local field_radio_freq = Field.new("avclan.radio.freq")
local field_radio_band = Field.new("avclan.radio.band")
local field_cd_disc = Field.new("avclan.cd.disc")
local field_cd_track = Field.new("avclan.cd.track")
local field_cd_min = Field.new("avclan.cd.mins")
local field_cd_sec = Field.new("avclan.cd.secs")

-- Attach the "not decoded" expert warning to a specific byte range rather than
-- the whole message, so Wireshark highlights exactly the bytes we don't yet
-- understand.
local function mark_undecoded(tree, range)
    tree:add(range, "Undecoded bytes"):add_proto_expert_info(pe_unhandled_msg)
end

-- Add a device-address field (from/to/active), attaching an expert note when the
-- byte isn't a known device so the frame is filterable via
-- `avclan.unknown_device.expert`. Returns the item so callers can chain off it.
local function add_device(tree, field, range)
    local item = tree:add(field, range)
    if known_devices[range:uint()] == nil then
        item:add_proto_expert_info(pe_unknown_device)
    end
    return item
end

-- Add the action field, attaching an expert note when the byte isn't a known
-- action (filterable via `avclan.unknown_action.expert`). Returns the item.
local function add_action(tree, range)
    local item = tree:add(f_action, range)
    if known_actions[range:uint()] == nil then
        item:add_proto_expert_info(pe_unknown_action)
    end
    return item
end

-- ---------------------------------------------------------------------------
-- Per-message decoders.
--
-- Each decoder receives the message `subtree`, the `buffer`, and `offset` (the
-- index of the `from` byte, accounting for the leading 0x00 on unicast frames).
-- Device-state decoders also receive the resolved `action`. The dissector body
-- below is just a router that selects one of these based on from/to/action,
-- preserving the historical precedence order.
-- ---------------------------------------------------------------------------

-- CMD_SW source. Sub-commands to the amplifier (actions 0x90-0x95) set a
-- tone/level control; these codes collide with physical-interface actions, so
-- the CMD_SW -> AUDIO_AMP device pair disambiguates them.
local function decode_cmd_sw(subtree, buffer, offset, to_device)
    add_action(subtree, buffer(offset+2,1))
    local action = field_action().value
    if to_device == known_devices_names["AUDIO_AMP"] then
        local amptree = subtree:add(avclanproto, buffer(offset+2,-1), "Device: Audio amplifier control")
        local param = buffer(offset+3,1)
        if action == 0x90 then -- VOLUME (BCD)
            local vol_raw = param:uint()
            amptree:add(f_amp_volume, param, bcd2dec(vol_raw) or vol_raw):append_text(" (VOLUME)")
        elseif action == 0x91 then
            amptree:add(f_amp_balance, param)
        elseif action == 0x92 then
            amptree:add(f_amp_fade, param)
        elseif action == 0x93 then
            amptree:add(f_amp_bass, param)
        elseif action == 0x94 then
            amptree:add(f_amp_mid, param)
        elseif action == 0x95 then
            amptree:add(f_amp_treble, param)
        end
    end
end

-- COMMUNICATION v1/v2 source.
local function decode_from_comm(subtree, buffer, offset, to_device)
    if to_device == known_devices_names["COMM_CTRL"] then
        add_action(subtree, buffer(offset+2,1))
        local action = field_action().value
        if action == known_actions_names["ADVERTISE_FUNCTION"] then
            add_device(subtree, f_active_device, buffer(offset+3,1))
        elseif action == known_actions_names["PING_REQ"] then
            subtree:add(f_ping_count, buffer(offset+3,1))
            subtree:add_proto_expert_info(pe_ping_req, "Ping request " .. buffer(offset+3,1):uint())
        elseif known_actions[action] then
        else
            subtree:add_proto_expert_info(pe_unhandled_msg)
        end
    else
        -- Control actions addressed to a peripheral rather than COMM_CTRL, e.g.
        -- function enable/disable (COMM_v1/v2 -> CD_CHANGER, 0x42/0x43). These
        -- are understood apart from a trailing 0x01 of unknown meaning.
        add_action(subtree, buffer(offset+2,1))
    end
end

-- COMM_CTRL source.
local function decode_from_commctrl(subtree, buffer, offset, to_device)
    if to_device == known_devices_names["COMMUNICATION v1"] or
    to_device == known_devices_names["COMMUNICATION v2"] then
        local action_tree = add_action(subtree, buffer(offset+2,1))
        local action = field_action().value
        if action == known_actions_names["PING_RESP"] then
            subtree:add(f_ping_count, buffer(offset+3,1))
            subtree:add_proto_expert_info(pe_ping_resp, "Ping response " .. buffer(offset+3,1):uint())
        elseif action == known_actions_names["LIST_FUNCTIONS_RESP"] then
            local functions = action_tree:add(f_functions, buffer(offset+3))
            functions:append_text(" (")
            for v = 0,(buffer:bytes(offset+3)):len()-1 do
                if known_devices[buffer(offset+3+v,1):uint()] then
                    functions:append_text(" " .. known_devices[buffer(offset+3+v,1):uint()])
                else
                    functions:append_text(" UNKNOWN_DEVICE")
                end
            end
            functions:append_text(" ) ")
        elseif known_actions[action] then
        else
            subtree:add_proto_expert_info(pe_unhandled_msg)
        end
    elseif to_device == known_devices_names["COMM_CTRL"] then
        local action_tree = add_action(subtree, buffer(offset+2,1))
        local action = field_action().value
        if action == known_actions_names["BACKLIGHT_ADJUST"] then
            local backlight = subtree:add(f_backlight, buffer(offset+3,1))
            backlight:append_text(" (" .. math.floor(100*(63 - buffer(offset+3,1):uint())/63) .. ")")
        end
    elseif to_device == known_actions_names["LANCHECK_SCAN_REQ"] or
        to_device == known_actions_names["LANCHECK_REQ"] or
        to_device == known_actions_names["LANCHECK_END_REQ"] then
        add_action(subtree, buffer(offset+1,1))
    elseif to_device == 0x00 then
        add_action(subtree, buffer(offset+2,1))
    else
        subtree:add_proto_expert_info(pe_unhandled_msg)
    end
end

-- Beep request (any source -> BEEP_SPEAKERS), action 0x60 with a duration byte.
local function decode_beep(subtree, buffer, offset)
    add_action(subtree, buffer(offset+2,1))
    local action = field_action().value
    if action == known_actions_names["BEEP"] then
        subtree:add(f_beep_duration, buffer(offset+3,1))
    else
        subtree:add_proto_expert_info(pe_unhandled_msg)
    end
end

-- TUNER source: radio state dump (regardless of action).
local function decode_radio(subtree, buffer, offset, action)
    local radiotree = subtree:add(avclanproto, buffer(offset), "Device: Radio")
    radiotree:add_le(f_radio_active, buffer(offset+3,1))
    radiotree:add_le(f_radio_status, buffer(offset+4,1))
    radiotree:add_le(f_radio_band, buffer(offset+5,1))
    radiotree:add(f_radio_bandnumber, buffer(offset+5,1))
    local freqtree = radiotree:add(f_radio_freq, buffer(offset+6,2))
    local radio_band = buffer(offset+5,1):uint()
    local freq = field_radio_freq().value
    if bit.band(radio_band, 0xF0) == 0x80 then
        freqtree:append_text(" (" .. 87.5+(freq-1)*.05 .. " MHz)")
    elseif bit.band(radio_band, 0xF0) == 0xC0 then
        freqtree:append_text(" (" .. 153+(freq-1)*1 .. " kHz)")
    elseif bit.band(radio_band, 0xF0) == 0x00 then
        freqtree:append_text(" (" .. 522+(freq-1)*9 .. " kHz)")
    end

    -- Preset channel number (@enkiusz's pd.py byte 6); 0 means "not on a preset".
    if buffer:len() > offset+8 then
        local channel = buffer(offset+8,1)
        radiotree:add(f_radio_channel, channel)
        if channel:uint() > 0 then
            radiotree:append_text(", preset #" .. channel:uint())
        end
    end

    -- Flags are offset-relative (@enkiusz's pd.py bytes 7 and 8); earlier
    -- versions of this dissector read them at the fixed absolute offsets 15/16.
    if buffer:len() > offset+9 then
        local flags = radiotree:add(f_radio_flags, buffer(offset+9,1))
        flags:add(f_radioflag_st, buffer(offset+9,1))
        flags:add(f_radioflag_ta, buffer(offset+9,1))
        flags:add(f_radioflag_reg, buffer(offset+9,1))
        flags:add(f_radioflag_af, buffer(offset+9,1))
    end
    if buffer:len() > offset+10 then
        radiotree:add(f_radio_flags2, buffer(offset+10,1))
    end
end

-- AUDIO_AMP source: amplifier state dump (regardless of action).
local function decode_amp(subtree, buffer, offset, action)
    -- Span to end of buffer rather than a fixed length: the flags byte (offset+14)
    -- isn't present in every amp frame, and a fixed range would run out of bounds.
    local amptree = subtree:add(avclanproto, buffer(offset), "Device: Audio amplifier")

    local vol_raw = buffer(offset+4,1):uint()
    amptree:add(f_amp_volume, buffer(offset+4,1), bcd2dec(vol_raw) or vol_raw)
    amptree:add(f_amp_balance, buffer(offset+5,1))
    amptree:add(f_amp_fade, buffer(offset+6,1))
    amptree:add(f_amp_bass, buffer(offset+7,1))
    amptree:add(f_amp_mid, buffer(offset+8,1))
    amptree:add(f_amp_treble, buffer(offset+9,1))

    -- Status flags (@enkiusz's pd.py byte 11); only MUTE (0x04) is identified.
    if buffer:len() > offset+14 then
        local amp_flags = amptree:add(f_amp_flags, buffer(offset+14,1))
        amp_flags:add(f_amp_mute, buffer(offset+14,1))
    end
end

-- CD / CD_CHANGER source: payload depends on the report action.
local function decode_cd(subtree, buffer, offset, action)
    if action == known_actions_names["PLAYBACK_STATUS"] or
        action == known_actions_names["PLAYBACK_RESP"] then
        local cdtree = subtree:add(avclanproto, buffer(offset,-1), "Device: CD player")
        local cd_slots = cdtree:add(f_cd_slots, buffer(offset+3,1))
        cd_slots:add(f_cd_slot1, buffer(offset+3,1))
        cd_slots:add(f_cd_slot2, buffer(offset+3,1))
        cd_slots:add(f_cd_slot3, buffer(offset+3,1))
        cd_slots:add(f_cd_slot4, buffer(offset+3,1))
        cd_slots:add(f_cd_slot5, buffer(offset+3,1))
        cd_slots:add(f_cd_slot6, buffer(offset+3,1))

        local cd_state = cdtree:add(f_cd_state, buffer(offset+4,1))
        cd_state:add(f_cd_open, buffer(offset+4,1))
        cd_state:add(f_cd_err1, buffer(offset+4,1))
        cd_state:add(f_cd_seeking, buffer(offset+4,1))
        cd_state:add(f_cd_playback, buffer(offset+4,1))
        cd_state:add(f_cd_seeking_track, buffer(offset+4,1))
        cd_state:add(f_cd_loading, buffer(offset+4,1))
        -- The sign of the play time lives in the trailing flags byte, so read it
        -- before formatting the time (older/shorter reports may omit it).
        local negative = false
        if buffer:len() > offset+10 then
            negative = bit.band(buffer(offset+10,1):uint(), 0x40) ~= 0
        end
        local cd_status = cdtree:add(avclanproto, buffer(offset+5,-1), "")
        cd_status:add(f_cd_disc, buffer(offset+5,1))
        cd_status:add(f_cd_track, buffer(offset+6,1))
        cd_status:add(f_cd_min, buffer(offset+7,1))
        cd_status:add(f_cd_sec, buffer(offset+8,1))
        cd_status:append_text("Disc " .. field_cd_disc().value .. ", ")
        cd_status:append_text("track " .. tostring(buffer(offset+6,1)):gsub("(.)(.)", "%1%2") .. ", ")
        cd_status:append_text("time " .. bcd_time(buffer(offset+7,1):uint(), buffer(offset+8,1):uint(), negative))
        local cd_flags = cdtree:add(f_cd_flags, buffer(offset+9,1))
        cd_flags:add(f_cd_flag_disk_random, buffer(offset+9,1))
        cd_flags:add(f_cd_flag_random, buffer(offset+9,1))
        cd_flags:add(f_cd_flag_disk_repeat, buffer(offset+9,1))
        cd_flags:add(f_cd_flag_repeat, buffer(offset+9,1))
        cd_flags:add(f_cd_flag_disk_scan, buffer(offset+9,1))
        cd_flags:add(f_cd_flag_scan, buffer(offset+9,1))
        if buffer:len() > offset+10 then
            local cd_flags2 = cdtree:add(f_cd_flags2, buffer(offset+10,1))
            cd_flags2:add(f_cd_flag_negative, buffer(offset+10,1))
            cd_flags2:add(f_cd_flag2_unknown7, buffer(offset+10,1))
            if negative then
                cd_flags2:append_text(" (play time is negative)")
            end
        end
    elseif action == known_actions_names["LOADING_STATUS"] or
        action == known_actions_names["LOADING_RESP"] then
        local cdtree = subtree:add(avclanproto, buffer(offset,9), "Device: CD player")
        local available_slots = cdtree:add(f_cd_slots, buffer(offset+4,1))
        available_slots:add(f_cd_slot1, buffer(offset+4,1))
        available_slots:add(f_cd_slot2, buffer(offset+4,1))
        available_slots:add(f_cd_slot3, buffer(offset+4,1))
        available_slots:add(f_cd_slot4, buffer(offset+4,1))
        available_slots:add(f_cd_slot5, buffer(offset+4,1))
        available_slots:add(f_cd_slot6, buffer(offset+4,1))

        local occupied_slots = cdtree:add(f_cd_slots, buffer(offset+6,1))
        occupied_slots:add(f_cd_slot1, buffer(offset+6,1))
        occupied_slots:add(f_cd_slot2, buffer(offset+6,1))
        occupied_slots:add(f_cd_slot3, buffer(offset+6,1))
        occupied_slots:add(f_cd_slot4, buffer(offset+6,1))
        occupied_slots:add(f_cd_slot5, buffer(offset+6,1))
        occupied_slots:add(f_cd_slot6, buffer(offset+6,1))

        local redundant_slots = cdtree:add(f_cd_slots, buffer(offset+8,1))
        redundant_slots:add(f_cd_slot1, buffer(offset+8,1))
        redundant_slots:add(f_cd_slot2, buffer(offset+8,1))
        redundant_slots:add(f_cd_slot3, buffer(offset+8,1))
        redundant_slots:add(f_cd_slot4, buffer(offset+8,1))
        redundant_slots:add(f_cd_slot5, buffer(offset+8,1))
        redundant_slots:add(f_cd_slot6, buffer(offset+8,1))

        local cd_state = cdtree:add(f_cd_state, buffer(offset+9,1))
        cd_state:add(f_cd_open, buffer(offset+9,1))
        cd_state:add(f_cd_err1, buffer(offset+9,1))
        cd_state:add(f_cd_seeking, buffer(offset+9,1))
        cd_state:add(f_cd_playback, buffer(offset+9,1))
        cd_state:add(f_cd_seeking_track, buffer(offset+9,1))
        cd_state:add(f_cd_loading, buffer(offset+9,1))
    elseif action == known_actions_names["REPORT_TOC"] then
        -- Disc table-of-contents (@enkiusz's REPORT_TOC layout).
        local toctree = subtree:add(avclanproto, buffer(offset), "Device: CD player (TOC)")
        toctree:add(f_cd_disc, buffer(offset+3,1))
        toctree:add(f_cd_track, buffer(offset+4,1)):append_text(" (first track)")
        toctree:add(f_cd_track_count, buffer(offset+5,1))
        local total = bcd_time(buffer(offset+6,1):uint(), buffer(offset+7,1):uint())
        toctree:append_text(", total time " .. total)
    elseif action == known_actions_names["TRACK_NAME_RESP"] then
        -- Track-name report: disc/track then an ASCII title (@enkiusz's pd.py:
        -- title starts at data byte 5).
        local nametree = subtree:add(avclanproto, buffer(offset,-1), "Device: CD player (track name)")
        nametree:add(f_cd_disc, buffer(offset+3,1))
        nametree:add(f_cd_track, buffer(offset+4,1))
        if buffer:len() > offset+7 then
            local title = nametree:add(f_cd_title, buffer(offset+7))
            title:append_text(" (\"" .. buffer(offset+7):string() .. "\")")
        end
    elseif action == known_actions_names["ENABLE_FUNCTION_RESP"] or
        action == known_actions_names["DISABLE_FUNCTION_RESP"] then
        -- Understood apart from a trailing 0x01 of unknown meaning; not flagged.
    elseif buffer:len() > offset+3 then
        -- Recognized CD action (initial report 0xf0, TOC 0xf9, track name
        -- 0xfd, ...) whose payload layout isn't understood yet. Flag just the
        -- undecoded bytes, not the whole (correctly-named) message.
        mark_undecoded(subtree, buffer(offset+3))
    end
end

-- CD / CD_CHANGER destination: requests addressed to a CD player. Only the
-- track-name request carries a decodable payload (@enkiusz's pkt_to_cd_player);
-- other request actions are identified by their action byte alone.
local function decode_to_cd(subtree, buffer, offset, action)
    if action == known_actions_names["TRACK_NAME_REQ"] then
        subtree:add(f_cd_disc, buffer(offset+3,1))
        subtree:add(f_cd_track, buffer(offset+4,1))
    end
end

-- TAPE_DECK source: tape state dump on PLAYBACK_STATUS
local function decode_tape(subtree, buffer, offset, action)
    if action == known_actions_names["PLAYBACK_STATUS"] then
        local tapetree = subtree:add(avclanproto, buffer(offset,4), "Device: Tape deck")
        tapetree:add(f_tape_present, buffer(offset+3,1))

        local tape_state = tapetree:add(f_tape_state, buffer(offset+4,1))
        tape_state:add(f_tape_seeking_rev, buffer(offset+4,1))
        tape_state:add(f_tape_err1, buffer(offset+4,1))
        tape_state:add(f_tape_playback, buffer(offset+4,1))
        tape_state:add(f_tape_seeking, buffer(offset+4,1))
        tape_state:add(f_tape_state1, buffer(offset+4,1))
        tape_state:add(f_tape_random, buffer(offset+4,1))

        local tape_flags = tapetree:add(f_tape_flags, buffer(offset+5,2))
        tape_flags:add(f_tape_stereo, buffer(offset+5,2))
        tape_flags:add(f_tape_dolby, buffer(offset+5,2))
        tape_flags:add(f_tape_flag1, buffer(offset+5,2))
        tape_flags:add(f_tape_flag2, buffer(offset+5,2))
        tape_flags:add(f_tape_flag3, buffer(offset+5,2))
        tape_flags:add(f_tape_flag4, buffer(offset+5,2))
    end
end

-- SW source: touch-screen press (action 0x78) with x,y position pairs.
local function decode_touch(subtree, buffer, offset, action)
    if action == known_actions_names["SCREEN_PRESS"] then
        subtree:add(f_touch_x, buffer(offset+3,1))
        subtree:add(f_touch_y, buffer(offset+4,1))
        if buffer:len() > offset+6 then
            subtree:add(f_touch_x, buffer(offset+5,1)):append_text(" (2)")
            subtree:add(f_touch_y, buffer(offset+6,1)):append_text(" (2)")
        end
    else
        subtree:add_proto_expert_info(pe_unhandled_msg)
    end
end

-- Device-state report decoders, keyed by the originating device. For these,
-- the from_device determines the payload format; the action (passed through)
-- only matters within CD/tape/touch.
local device_decoders = {
    [known_devices_names["TUNER"]]      = decode_radio,
    [known_devices_names["AUDIO_AMP"]]  = decode_amp,
    [known_devices_names["CD_SINGLE"]]         = decode_cd,
    [known_devices_names["CD_CHANGER"]] = decode_cd,
    [known_devices_names["TAPE_DECK"]]  = decode_tape,
    [known_devices_names["SW_AUDIO"]]         = decode_touch,
}

function avclanproto.dissector(buffer, pinfo, tree)
    local length = buffer:len()
    if length == 0 then
        return
    end

    iebusproto.dissector(buffer, pinfo, tree)

    local subtree = tree:add(avclanproto, buffer(7,-1), "AVCLAN message")
    -- Unicast device-to-device frames carry a leading 0x00 before the from/to
    -- bytes; skip it so `offset` always points at the `from` byte.
    local offset = 7
    if buffer(7,1):uint() == 0 then
        offset = 8
    elseif buffer(0,1):uint() ~= 0 then
        -- Unicast frame whose leading byte isn't the expected 0x00 pad (e.g.
        -- 0xff). @enkiusz notes both 0x00 and 0xff occur here, but under the
        -- current protocol understanding a non-0x00 prefix is unusual; rather
        -- than risk misaligning every downstream field, treat it conservatively
        -- as undecoded.
        subtree:add_proto_expert_info(pe_unhandled_msg)
        return
    end
    add_device(subtree, f_from_device, buffer(offset+0,1))
    add_device(subtree, f_to_device, buffer(offset+1,1))

    local from_device = field_from_device().value
    local to_device = field_to_device().value

    if from_device == known_devices_names["CMD_SW"] then
        decode_cmd_sw(subtree, buffer, offset, to_device)
    elseif from_device == known_devices_names["COMMUNICATION v1"] or
      from_device == known_devices_names["COMMUNICATION v2"] then
        decode_from_comm(subtree, buffer, offset, to_device)
    elseif from_device == known_devices_names["COMM_CTRL"] then
        decode_from_commctrl(subtree, buffer, offset, to_device)
    elseif to_device == known_devices_names["BEEP_SPEAKERS"] then
        decode_beep(subtree, buffer, offset)
    elseif from_device == known_devices_names["STATUS"] and
        known_devices[to_device] ~= nil then
        add_action(subtree, buffer(offset+2,1))
    elseif device_decoders[from_device] then
        add_action(subtree, buffer(offset+2,1))
        device_decoders[from_device](subtree, buffer, offset, field_action().value)
    elseif to_device == known_devices_names["CD"] or
        to_device == known_devices_names["CD_CHANGER"] then
        add_action(subtree, buffer(offset+2,1))
        decode_to_cd(subtree, buffer, offset, field_action().value)
    else
        -- Unknown source/dest: the [from, to, action] header layout still holds
        -- even when we can't decode the body, so surface the action.
        if buffer:len() > offset+2 then
            add_action(subtree, buffer(offset+2,1))
        end
        subtree:add_proto_expert_info(pe_unhandled_msg)
    end
end

-- for i,v in ipairs(DissectorTable.list()) do print(v) end
udlt:add(wtap.USER15, avclanproto)
