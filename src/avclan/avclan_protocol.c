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

#include <stdint.h>
#include <string.h>

#include "avclan_phy.h" // AVCLAN_ismuted
#include "avclan_protocol.h"
#include "cdchanger.h"
#include "mediacontrol.h"
#include "statustimer.h"

#define PACK3(a, b, c) (((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (c))

static const uint8_t cdloading_resp[] = {dev_CD_CHANGER,
                                         dev_STATUS,
                                         Loading_Status_Report,
                                         0x00,
                                         0x01,
                                         0x00,
                                         0x01,
                                         0x00,
                                         0x01,
                                         0x02};

response_t AVCLAN_handleframe(const AVCLAN_frame_t *in, AVCLAN_frame_t *out) {
  response_t respond = r_Nothing;

  if (AVCLAN_ismuted() || in->length < 3)
    return respond;

  // 0xFF placeholders are variant bytes filled by writing directly to
  // out->data[N] after memcpy.
  static const uint8_t lancheck_resp[] = {0x00, dev_COMM_CTRL, dev_LAN, 0xFF,
                                          0xFF};
  static const uint8_t function_change_resp[] = {0x00, dev_CD_CHANGER,
                                                 dev_COMM_v1, 0xFF, 0x01};

  out->controller_addr = DEVICE_ADDR;
  out->control = 0xF;

  const uint8_t *data = in->data;
  const uint8_t b0 = *data++;
  const uint8_t b1 = *data++;
  const uint8_t b2 = *data++;
  uint8_t b3 = 0;
  if (in->length > 3) // the shortest known/valid messages are 3 bytes long
    b3 = *data++;

  if (!in->is_unicast) {
    // Broadcast: bytes are (from, to, action, [extra...]).
    // peripheral_addr unchecked — always 0xFFF or 0x1FF in known traffic.
    switch (PACK3(b0, b1, b2)) {
      case PACK3(dev_LAN, dev_COMM_CTRL, Lancheck_Scan_Req):
        out->length = sizeof(lancheck_resp);
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        memcpy(out->data, lancheck_resp, sizeof(lancheck_resp));
        out->data[3] = Lancheck_Scan_Resp;
        out->data[4] = 0x01;
        respond = r_Handled;
        break;
      case PACK3(dev_LAN, dev_COMM_CTRL, Lancheck_Req):
        out->length = sizeof(lancheck_resp);
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        memcpy(out->data, lancheck_resp, sizeof(lancheck_resp));
        out->data[3] = Lancheck_Resp;
        out->data[4] = 0x00;
        respond = r_Handled;
        break;
      case PACK3(dev_LAN, dev_COMM_CTRL, Lancheck_End_Req):
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        out->length = sizeof(lancheck_resp) - 1;
        memcpy(out->data, lancheck_resp, out->length);
        out->data[3] = Lancheck_End_Resp;
        respond = r_Handled;
        break;
      case PACK3(dev_COMM_v1, dev_COMM_CTRL, Current_Function):
      case PACK3(dev_COMM_v2, dev_COMM_CTRL, Current_Function):
        if ((b3 == dev_CD_CHANGER) && !AVCLAN_isPlaying()) {
          if (cd_status.mins > 99)
            cd_status.mins = 0;
          if (cd_status.secs > 99)
            cd_status.secs = 0;
          cd_status.state = cd_SEEKING | cd_SEEKING_TRACK;
          cd_status.flags2 = 0xc0;
          AVCLAN_generateStatus(out, true, dev_STATUS);
          respond = r_StartPlaying;
        }
        break;
      case PACK3(dev_COMM_v1, dev_COMM_CTRL, Ping_Req):
      case PACK3(dev_COMM_v2, dev_COMM_CTRL, Ping_Req): {
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        const uint8_t ping_resp[] = {0x00,      dev_COMM_CTRL, dev_COMM_v1,
                                     Ping_Resp, 0xFF,          b3};
        out->length = sizeof(ping_resp);
        memcpy(out->data, ping_resp, sizeof(ping_resp));
        respond = r_Handled;
        break;
      }
      case PACK3(dev_COMM_v1, dev_COMM_CTRL, List_Functions_Req):
      case PACK3(dev_COMM_v2, dev_COMM_CTRL, List_Functions_Req): {
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        const uint8_t list_functions_resp[] = {0x00, dev_COMM_CTRL, dev_COMM_v1,
                                               List_Functions_Resp,
                                               dev_CD_CHANGER};
        out->length = sizeof(list_functions_resp);
        memcpy(out->data, list_functions_resp, sizeof(list_functions_resp));
        respond = r_Handled;
        break;
      }
        // case Restart_Lan: not handled
    }
  } else if (in->peripheral_addr == DEVICE_ADDR && b0 == 0x00) {
    // Unicast to CD changer: bytes are (0x00, from, to, action, [extra...]).
    switch (PACK3(b1, b2, b3)) {
      case PACK3(dev_COMM_v1, dev_CD_CHANGER, Enable_Function_Req):
        [[fallthrough]];
      case PACK3(dev_COMM_v2, dev_CD_CHANGER, Enable_Function_Req):
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        out->length = sizeof(function_change_resp);
        memcpy(out->data, function_change_resp, sizeof(function_change_resp));
        out->data[3] = Enable_Function_Resp;
        cd_status.state = 0;
        cd_status.flags2 = 0x80;
        respond = r_StatusReport;
        break;
      case PACK3(dev_COMM_v1, dev_CD_CHANGER, Disable_Function_Req):
        [[fallthrough]];
      case PACK3(dev_COMM_v2, dev_CD_CHANGER, Disable_Function_Req):
        // No change/response needed if we're already not playing
        if (AVCLAN_isPlaying()) {
          AVCLAN_stopPlaying();
          out->length = sizeof(function_change_resp);
          memcpy(out->data, function_change_resp, sizeof(function_change_resp));
          out->data[3] = Disable_Function_Resp;
          cd_status.state = 0;
          cd_status.flags2 = 0x80;
          out->is_unicast = true;
          out->peripheral_addr = HU_ADDR;
          respond = r_StatusReport;
        }
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Eject): {
        // "Eject" label is multiply wrong; proper meaning unclear:
        //    - First observed on initial multiple presses of "CD" button,
        //    triggering (after {0x00, dev_CD_CHANGER, dev_COMM_v1, Insertion,
        //    0x01} response) proper activation of mockingboard/cd-changer.
        //    - Subsequently observed when pressing (technically
        //    releasing?) the fast-forward button and rewind
        if (cd_status.state | cd_SEEKING) { // FF/RW button released
          cd_status.state &= ~cd_SEEKING;
        } else {
          out->is_unicast = true;
          out->peripheral_addr = HU_ADDR;
          {
            const uint8_t msg[] = {0x00, dev_CD_CHANGER, dev_CMD_SW, Insertion,
                                   0x01};
            out->length = sizeof(msg);
            memcpy(out->data, msg, sizeof(msg));
          }
          respond = r_Handled;
        }
        break;
      }
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Initial_Report_Request):
        [[fallthrough]];
      case PACK3(dev_STATUS, dev_CD_CHANGER, Initial_Report_Request): {
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;

        // No knowledge/understanding of field meaning/interpretation
        const uint8_t cdinitreport_resp[] = {
            0x00, dev_CD_CHANGER, b1,  Initial_Report_Response, 0x01, 0x31,
            0x10, 0x01,           0x01};
        out->length = sizeof(cdinitreport_resp);
        memcpy(&out->data[1], cdinitreport_resp, sizeof(cdinitreport_resp));
        respond = r_Handled;
        break;
      }
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Playback_Request): [[fallthrough]];
      case PACK3(dev_STATUS, dev_CD_CHANGER, Playback_Request):
        out->data[0] = 0x00;
        out->data[1] = dev_CD_CHANGER;
        out->data[2] = b1;
        out->data[3] = Playback_Report;
        out->length = sizeof(AVCLAN_CD_Status_t) + 4;
        serializeCDStatus(&out->data[4]);
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        respond = r_Handled;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Loading_Request2): [[fallthrough]];
      case PACK3(dev_STATUS, dev_CD_CHANGER, Loading_Request2):
        out->data[0] = 0x00;
        out->length = sizeof(cdloading_resp) + 1;
        memcpy(&out->data[1], cdloading_resp, sizeof(cdloading_resp));
        out->data[2] = b1;
        out->data[3] = Loading_Response2;
        out->is_unicast = true;
        out->peripheral_addr = HU_ADDR;
        respond = r_Handled;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Track_Seek_Up):
        cd_status.state = cd_SEEKING_TRACK;
        if (cd_status.track < 98)
          ++cd_status.track;
        else
          cd_status.track = 1;
        cd_status.mins = 0xff;
        cd_status.secs = 0x7f;
        cd_status.flags2 = 0xc0;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        AVCLAN_micSkipForward();
        respond = r_TrackChange;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Track_Seek_Down):
        cd_status.state = cd_SEEKING_TRACK;
        // Track down returns to track beginning if in ~middle of song
        if (cd_status.mins == 0 && cd_status.secs < 0x05) {
          if (cd_status.track > 1)
            --cd_status.track;
          else
            cd_status.track = 99;
        }
        cd_status.mins = 0xff;
        cd_status.secs = 0x7f;
        cd_status.flags2 = 0xc0;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        AVCLAN_micSkipBackward();
        respond = r_TrackChange;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Track_Fast_Forward): {
        cd_status.state |= cd_SEEKING;
        cd_status.secs += 15;
        if (cd_status.secs > 60) {
          cd_status.secs -= 60;
          ++cd_status.mins;
        }
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        AVCLAN_micSkipForward();
        statustimer_reset(); // Skipped to a whole/round sec; ensure next tick is
                             // ~1 sec from now
        respond = r_Handled;
        break;
      }
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, Track_Rewind): {
        cd_status.state |= cd_SEEKING;
        if (cd_status.secs < 15) {
          if (cd_status.mins > 0) {
            uint8_t d = 15 - cd_status.secs;
            cd_status.secs = 60 - d;
            --cd_status.mins;
          } else {
            cd_status.mins = 0;
            cd_status.secs = 0;
          }
        } else
          cd_status.secs -= 15;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        AVCLAN_micSkipBackward();
        statustimer_reset(); // Skipped to a whole/round sec; ensure next tick is
                             // ~1 sec from now
        respond = r_Handled;
        break;
      }
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Enable_Random):
        cd_status.flags |= cd_RANDOM;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Disable_Random):
        cd_status.flags &= ~cd_RANDOM;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Enable_Repeat):
        cd_status.flags |= cd_REPEAT;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Disable_Repeat):
        cd_status.flags &= ~cd_REPEAT;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Enable_Disk_Random):
        cd_status.flags |= cd_DISK_RANDOM;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Disable_Disk_Random):
        cd_status.flags &= ~cd_DISK_RANDOM;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Enable_Disk_Repeat):
        cd_status.flags |= cd_DISK_REPEAT;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
      case PACK3(dev_CMD_SW, dev_CD_CHANGER, CD_Disable_Disk_Repeat):
        cd_status.flags &= ~cd_DISK_REPEAT;
        AVCLAN_generateStatus(out, true, dev_CMD_SW);
        respond = r_StatusReport;
        break;
    }
  }

  return respond;
}

#undef PACK3

RFrame_t *AVCLAN_statemachine(RFrame_t *resp) {
  AVCLAN_frame_t *out = resp->frame;
  switch (resp->r) {
    case r_Ejection: {
      const uint8_t play[] = {0x00,      dev_COMM_CTRL,  dev_COMM_v1,
                              Insertion, dev_CD_CHANGER, 0x01};
      out->length = sizeof(play);
      memcpy(out->data, play, sizeof(play));
    }
      resp->r = r_Report_Load;
      break;
    case r_Report_Load:
      out->is_unicast = false;
      out->peripheral_addr = 0x1FF;
      out->length = sizeof(cdloading_resp) + 1;
      memcpy(out->data, cdloading_resp, sizeof(cdloading_resp));
      out->data[1] = dev_STATUS;
      out->data[2] = Loading_Status_Report;
      resp->r = r_Handled;
      break;
    case r_TrackChange:
      AVCLAN_setTime(0x00, 0x00);
      statustimer_reset(); // Skipped to a whole/round sec; ensure next tick is
                           // ~1 sec from now
      [[fallthrough]];
    case r_NormalizeState:
      AVCLAN_normalizeState();
      AVCLAN_generateStatus(out, true, dev_STATUS);
      resp->r = r_Handled;
      break;
    case r_StartPlaying:
      AVCLAN_normalizeState();
      AVCLAN_generateStatus(out, true, dev_STATUS);
      resp->r = r_BeganPlaying;
      break;
    case r_BeganPlaying:
      AVCLAN_startPlaying(); // only start PIT after normalizing state
      resp->r = r_Nothing;
      break;
    case r_StatusReport:
      AVCLAN_generateStatus(out, true, dev_STATUS);
      resp->r = r_Handled;
      break;
    case r_Handled: [[fallthrough]];
    case r_Nothing: [[fallthrough]];
    default: resp->r = r_Nothing;
  }
  return resp;
}
