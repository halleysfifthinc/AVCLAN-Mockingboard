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

local f_broadcast = ProtoField.bool("iebus.broadcast", "Broadcast", base.NONE, { [1] = "false", [2] = "true" })
local f_controller_addr = ProtoField.uint16("iebus.controller", "Controller address", base.HEX, nil, 0x0FFF)
local f_peripheral_addr = ProtoField.uint16("iebus.peripheral", "Peripheral address", base.HEX, nil, 0x0FFF)
local f_control = ProtoField.uint8("iebus.control", "Control field", base.HEX)
local f_length = ProtoField.uint8("iebus.length", "Data length", base.DEC)
local f_data = ProtoField.bytes("iebus.data", "Frame data", base.SPACE)

iebusproto.fields = {
    f_broadcast,
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

    subtree:add_le(f_broadcast, buffer(0,1))
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

local known_devices_names = {
    ["COMM_CTRL"] = 0x01,
    ["COMMUNICATION v1"] = 0x11,
    ["COMMUNICATION v2"] = 0x12,
    ["SW"] = 0x21,
    ["SW_NAME"] = 0x23,
    ["SW_CONVERTING"] = 0x24,
    ["CMD_SW"] = 0x25,
    ["STATUS"] = 0x31,
    ["BEEP_HU"] = 0x28,
    ["BEEP_SPEAKERS"] = 0x29,
    ["FRONT_PSNG_MONITOR"] = 0x34,
    ["CD_CHANGER2"] = 0x43,
    ["BLUETOOTH_TEL"] = 0x55,
    ["INFO_DRAWING"] = 0x56,
    ["NAV_ECU"] = 0x58,
    ["CAMERA"] = 0x5C,
    ["CLIMATE_DRAWING"] = 0x5D,
    ["AUDIO_DRAWING"] = 0x5E,
    ["TRIP_INFO_DRAWING"] = 0x5F,
    ["TUNER"] = 0x60,
    ["TAPE_DECK"] = 0x61,
    ["CD"] = 0x62,
    ["CD_CHANGER"] = 0x63,
    ["AUDIO_AMP"] = 0x74,
    ["GPS"] = 0x80,
    ["VOICE_CTRL"] = 0x85,
    ["CLIMATE_CTRL_DEV"] = 0xE0,
    ["TRIP_INFO"] = 0xE5,
}

local known_devices = {
    [0x01] = "COMM_CTRL",
    [0x11] = "COMMUNICATION v1",
    [0x12] = "COMMUNICATION v2",
    [0x21] = "SW",
    [0x23] = "SW_NAME",
    [0x24] = "SW_CONVERTING",
    [0x25] = "CMD_SW",
    [0x31] = "STATUS",
    [0x28] = "BEEP_HU",
    [0x29] = "BEEP_SPEAKERS",
    [0x34] = "FRONT_PSNG_MONITOR",
    [0x43] = "CD_CHANGER2",
    [0x55] = "BLUETOOTH_TEL",
    [0x56] = "INFO_DRAWING",
    [0x58] = "NAV_ECU",
    [0x5C] = "CAMERA",
    [0x5D] = "CLIMATE_DRAWING",
    [0x5E] = "AUDIO_DRAWING",
    [0x5F] = "TRIP_INFO_DRAWING",
    [0x60] = "TUNER",
    [0x61] = "TAPE_DECK",
    [0x62] = "CD",
    [0x63] = "CD_CHANGER",
    [0x74] = "AUDIO_AMP",
    [0x80] = "GPS",
    [0x85] = "VOICE_CTRL",
    [0xE0] = "CLIMATE_CTRL_DEV",
    [0xE5] = "TRIP_INFO",
}

local f_from_device = ProtoField.uint8("avclan.from_device", "From device", base.HEX, known_devices)
local f_to_device = ProtoField.uint8("avclan.to_device", "To device", base.HEX, known_devices)
local f_active_device = ProtoField.uint8("avclan.active_device", "Active device", base.HEX, known_devices)

local known_actions_names = {
    -- LAN related
    ["LIST_FUNCTIONS_REQ"] = 0x00,
    ["LIST_FUNCTIONS_RESP"] = 0x10,
    ["RESTART_LAN"] = 0x01,
    ["LANCHECK_END_REQ"] = 0x08,
    ["LANCHECK_END_RESP"] = 0x18,
    ["LANCHECK_SCAN_REQ"] = 0x0a,
    ["LANCHECK_SCAN_RESP"] = 0x1a,
    ["LANCHECK_REQ"] = 0x0c,
    ["LANCHECK_RESP"] = 0x1c,
    ["PING_REQ"] = 0x20,
    ["PING_RESP"] = 0x30,

    -- Device switching
    ["DISABLE_FUNCTION_REQ"] = 0x43,
    ["DISABLE_FUNCTION_RESP"] = 0x53,
    ["ENABLE_FUNCTION_REQ"] = 0x42,
    ["ENABLE_FUNCTION_RESP"] = 0x52,

    ["ADVERTISE_FUNCTION"] = 0x45,
    ["GENERAL_QUERY"] = 0x46,

    -- Physical interface
    ["EJECT"] = 0x80,
    ["DISC_UP"] = 0x90,
    ["DISC_DOWN"] = 0x91,
    ["PWRVOL_KNOB_RIGHTHAND_TURN"] = 0x9c,
    ["PWRVOL_KNOB_LEFTHAND_TURN"] = 0x9d,
    ["TRACK_SEEK_UP"] = 0x94,
    ["TRACK_SEEK_DOWN"] = 0x95,
    ["CD_ENABLE_SCAN"] = 0xa6,
    ["CD_DISABLE_SCAN"] = 0xa7,
    ["CD_ENABLE_REPEAT"] = 0xa0,
    ["CD_DISABLE_REPEAT"] = 0xa1,
    ["CD_ENABLE_RANDOM"] = 0xb0,
    ["CD_DISABLE_RANDOM"] = 0xb1,

    -- CD functions
    -- Events
    ["INSERTED_CD"] = 0x50,
    ["REMOVED_CD"] = 0x51,

    -- Requests
    ["REQUEST_REPORT"] = 0xe0,
    ["REQUEST_REPORT2"] = 0xe2,
    ["REQUEST_LOADER2"] = 0xe4,
    ["REQUEST_TRACK_NAME"] = 0xed,

    -- Reports
    ["REPORT"] = 0xf1,
    ["REPORT2"] = 0xf2,
    ["REPORT_LOADER"] = 0xf3,
    ["REPORT_LOADER2"] = 0xf4,
    ["REPORT_TOC"] = 0xf9,
    ["REPORT_TRACK_NAME"] = 0xfd,
}
local known_actions = {
    -- LAN related
    [0x00] = "LIST_FUNCTIONS_REQ",
    [0x10] = "LIST_FUNCTIONS_RESP",
    [0x01] = "RESTART_LAN",
    [0x08] = "LANCHECK_END_REQ",
    [0x18] = "LANCHECK_END_RESP",
    [0x0a] = "LANCHECK_SCAN_REQ",
    [0x1a] = "LANCHECK_SCAN_RESP",
    [0x0c] = "LANCHECK_REQ",
    [0x1c] = "LANCHECK_RESP",
    [0x20] = "PING_REQ",
    [0x30] = "PING_RESP",

    -- Used when HU is switching between Radio and CD
    [0x43] = "DISABLE_FUNCTION_REQ",
    [0x53] = "DISABLE_FUNCTION_RESP",
    [0x42] = "ENABLE_FUNCTION_REQ",
    [0x52] = "ENABLE_FUNCTION_RESP",

    [0x45] = "ADVERTISE_FUNCTION",
    [0x46] = "GENERAL_QUERY",

    -- Physical interface
    [0x80] = "EJECT",
    [0x90] = "DISC_UP",
    [0x91] = "DISC_DOWN",
    [0x9c] = "PWRVOL_KNOB_RIGHTHAND_TURN",
    [0x9d] = "PWRVOL_KNOB_LEFTHAND_TURN",
    [0x94] = "TRACK_SEEK_UP",
    [0x95] = "TRACK_SEEK_DOWN",
    [0xa6] = "CD_ENABLE_SCAN",
    [0xa7] = "CD_DISABLE_SCAN",
    [0xa0] = "CD_ENABLE_REPEAT",
    [0xa1] = "CD_DISABLE_REPEAT",
    [0xb0] = "CD_ENABLE_RANDOM",
    [0xb1] = "CD_DISABLE_RANDOM",

    -- CD functions
    -- Events
    [0x50] = "INSERTED_CD",
    [0x51] = "REMOVED_CD",

    -- Requests
    [0xe0] = "REQUEST_REPORT",
    [0xe2] = "REQUEST_REPORT2",
    [0xe4] = "REQUEST_LOADER2",
    [0xed] = "REQUEST_TRACK_NAME",

    -- Reports
    [0xf1] = "REPORT",
    [0xf2] = "REPORT2",
    [0xf3] = "REPORT_LOADER",
    [0xf4] = "REPORT_LOADER2",
    [0xf9] = "REPORT_TOC",
    [0xfd] = "REPORT_TRACK_NAME",
}

local f_action = ProtoField.uint8("avclan.action", "Action", base.HEX, known_actions)
local f_functions = ProtoField.bytes("avclan.functions", "Functions", base.SPACE, "Device functions")

local f_ping_count = ProtoField.uint8("avclan.ping.count", "Ping count")

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

local f_radioflag_st = ProtoField.bool("avclan.radio.flags.st", "Stereo", 8, {"Set", "Not set"}, 0x04)
local f_radioflag_ta = ProtoField.bool("avclan.radio.flags.ta", "TA", 8, {"Set", "Not set"}, 0x08)
local f_radioflag_reg = ProtoField.bool("avclan.radio.flags.reg", "REG", 8, {"Set", "Not set"}, 0x10)
local f_radioflag_af = ProtoField.bool("avclan.radio.flags.af", "AF", 8, {"Set", "Not set"}, 0x40)

local f_radio_band = ProtoField.uint8("avclan.radio.band", "Radio band", base.HEX, 
    {[0x8] = "FM", [0xC] = "AM (Long-wave)", [0x0] = "AM (Medium-wave)"}, 0xF0)
local f_radio_bandnumber = ProtoField.int8("avclan.radio.bandnumber", "Radio band number", base.DEC, nil, 0x0F)
local f_radio_freq = ProtoField.uint16("avclan.radio.freq", "Radio frequency")

local f_amp_volume = ProtoField.uint8("avclan.amp.volume", "Volume", base.DEC)
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
local f_cd_min = ProtoField.uint8("avclan.cd.mins", "CD track play time, minutes", base.HEX)
local f_cd_sec = ProtoField.uint8("avclan.cd.secs", "CD track play time, seconds", base.HEX)

local f_cd_flags = ProtoField.uint8("avclan.cd.flags", "CD player flags", base.HEX, {
    [0x02] = "DISK_RANDOM",
    [0x04] = "RANDOM",
    [0x08] = "DISK_REPEAT",
    [0x10] = "REPEAT",
    [0x20] = "DISK_SCAN",
    [0x40] = "SCAN",
})

avclanproto.fields = {
    f_from_device,
    f_to_device,
    f_active_device,
    f_action,
    f_functions,
    f_ping_count,
    f_radio_active,
    f_radio_status,
    f_radio_flags,
    f_radio_flags2,
    f_radio_band,
    f_radio_bandnumber,
    f_radio_freq,
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
    f_cd_min,
    f_cd_sec,
    f_cd_flags,
}

local pe_unhandled_msg = ProtoExpert.new("avclan.expert", "Message not decoded",
    expert.group.UNDECODED, expert.severity.WARN)
-- local pe_lan_check = ProtoExpert.new("avclan.lan.expert", "")
local pe_ping_req = ProtoExpert.new("avclan.ping_req.expert", "Ping request",
    expert.group.SEQUENCE, expert.severity.CHAT)
local pe_ping_resp = ProtoExpert.new("avclan.ping_resp.expert", "Ping response",
    expert.group.SEQUENCE, expert.severity.CHAT)

avclanproto.experts = {
    pe_unhandled_msg,
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

function avclanproto.dissector(buffer, pinfo, tree)
    local length = buffer:len()
    if length == 0 then
        return
    end

    iebusproto.dissector(buffer, pinfo, tree)

    local subtree = tree:add(avclanproto, buffer(7,-1), "AVCLAN message")
    local offset = 7
    if buffer(7,1):uint() == 0 then
        offset = 8
        subtree:add(f_from_device, buffer(offset+0,1))
        subtree:add(f_to_device, buffer(offset+1,1))
    else
        subtree:add(f_from_device, buffer(offset+0,1))
        subtree:add(f_to_device, buffer(offset+1,1))
    end

    local from_device = field_from_device().value
    local to_device = field_to_device().value

    if from_device == known_devices_names["CMD_SW"] then
        subtree:add(f_action, buffer(offset+2,1))
    elseif from_device == known_devices_names["COMMUNICATION v1"] or
      from_device == known_devices_names["COMMUNICATION v2"] then
        if to_device == known_devices_names["COMM_CTRL"] then
            subtree:add(f_action, buffer(offset+2,1))
            local action = field_action().value
            if action == known_actions_names["ADVERTISE_FUNCTION"] then
                subtree:add(f_active_device, buffer(offset+3,1))
            elseif action == known_actions_names["PING_REQ"] then
                subtree:add(f_ping_count, buffer(offset+3,1))
                subtree:add_proto_expert_info(pe_ping_req, "Ping request " .. buffer(offset+3,1):uint())
            elseif known_actions[action] then
            else
                subtree:add_proto_expert_info(pe_unhandled_msg)
            end
        else
            subtree:add_proto_expert_info(pe_unhandled_msg)
        end
    elseif from_device == known_devices_names["COMM_CTRL"] then
        if to_device == known_devices_names["COMMUNICATION v1"] or
        to_device == known_devices_names["COMMUNICATION v2"] then
            local action_tree = subtree:add(f_action, buffer(offset+2,1))
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
        elseif to_device == known_actions_names["LANCHECK_SCAN_REQ"] or
            to_device == known_actions_names["LANCHECK_REQ"] or
            to_device == known_actions_names["LANCHECK_END_REQ"] then
            subtree:add(f_action, buffer(offset+1,1))
        elseif to_device == 0x00 then
            subtree:add(f_action, buffer(offset+2,1))
        else
            subtree:add_proto_expert_info(pe_unhandled_msg)
        end
    elseif from_device == known_devices_names["STATUS"] then
        subtree:add(f_action, buffer(offset+2,1))
    elseif from_device == known_devices_names["TUNER"] then
        subtree:add(f_action, buffer(offset+2,1))
        local radiotree = subtree:add(avclanproto, buffer(offset,10), "Device: Radio")
        radiotree:add_le(f_radio_active, buffer(offset+3,1))
        radiotree:add_le(f_radio_status, buffer(offset+4,1))
        radiotree:add_le(f_radio_band, buffer(offset+5,1))
        radiotree:add(f_radio_bandnumber, buffer(offset+5,1))
        local freqtree = radiotree:add(f_radio_freq, buffer(offset+6,2))
        local radio_band = buffer(offset+5,1):uint()
        local freq = field_radio_freq().value
        if bit32.band(radio_band, 0xF0) == 0x80 then
            freqtree:append_text(" (" .. 87.5+(freq-1)*.05 .. " MHz)")
        elseif bit32.band(radio_band, 0xF0) == 0xC0 then
            freqtree:append_text(" (" .. 153+(freq-1)*1 .. " kHz)")
        elseif bit32.band(radio_band, 0xF0) == 0x00 then
            freqtree:append_text(" (" .. 522+(freq-1)*9 .. " kHz)")
        end

        local flags = radiotree:add(f_radio_flags, buffer(15,1))
        flags:add(f_radioflag_st, buffer(15,1))
        flags:add(f_radioflag_ta, buffer(15,1))
        flags:add(f_radioflag_reg, buffer(15,1))
        flags:add(f_radioflag_af, buffer(15,1))
        radiotree:add(f_radio_flags2, buffer(16,1))
    elseif from_device == known_devices_names["AUDIO_AMP"] then
        subtree:add(f_action, buffer(offset+2,1))
        local amptree = subtree:add(avclanproto, buffer(offset,10), "Device: Audio amplifier")
        
        amptree:add(f_amp_volume, buffer(offset+4,1))
        amptree:add(f_amp_balance, buffer(offset+5,1))
        amptree:add(f_amp_fade, buffer(offset+6,1))
        amptree:add(f_amp_bass, buffer(offset+7,1))
        amptree:add(f_amp_mid, buffer(offset+8,1))
        amptree:add(f_amp_treble, buffer(offset+9,1))
    elseif from_device == known_devices_names["CD"] or
      from_device == known_devices_names["CD_CHANGER"] or
      from_device == known_devices_names["CD_CHANGER2"] then
        subtree:add(f_action, buffer(offset+2,1))

        local action = field_action().value
        if action == known_actions_names["REPORT"] then
            local cdtree = subtree:add(avclanproto, buffer(offset,9), "Device: CD player")
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
            local cd_status = cdtree:add(avclanproto, buffer(offset+5,-1), "")
            cd_status:add(f_cd_disc, buffer(offset+5,1))
            cd_status:add(f_cd_track, buffer(offset+6,1))
            cd_status:add(f_cd_min, buffer(offset+7,1))
            cd_status:add(f_cd_sec, buffer(offset+8,1))
            cd_status:append_text("Disc " .. field_cd_disc().value .. ", ")
            cd_status:append_text("track " .. tostring(buffer(offset+6,1)):gsub("(.)(.)", "%1%2") .. ", ")
            cd_status:append_text("time " .. tostring(buffer(offset+7,1)):gsub("0x(.)(.)", "%1%2") .. ":")
            cd_status:append_text(tostring(buffer(offset+8,1)):gsub("(.)(.)", "%1%2"))
            cdtree:add(f_cd_flags, buffer(offset+9,1))
        end
    else
        subtree:add_proto_expert_info(pe_unhandled_msg)
    end
end

-- for i,v in ipairs(DissectorTable.list()) do print(v) end
udlt:add(wtap.USER15, avclanproto)
