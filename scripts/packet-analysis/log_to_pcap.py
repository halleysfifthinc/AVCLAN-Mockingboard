#!/usr/bin/env python3
"""One-off: convert a bracketed-timestamp AVC-LAN `log` dump into a pcap.

The capture in `msgdumps/log` is a foreign text format (not the firmware's):

    [ 08:50:12.793 ] 1901F1040025C0EF

Each hex blob is a tight nibble packing of an AVC-LAN frame, with no spaces and
no leading is_unicast byte (unlike the firmware text format that
extcap/avclan-pcap.py's --convert handles):

    controller : 3 hex nibbles (12-bit address)
    peripheral : 3 hex nibbles (12-bit address)
    length     : 2 hex nibbles (1 byte: data byte count)
    data       : length bytes  (2*length hex nibbles)

There is no control field in this format; the dissector wants one, so we
synthesize the standard AVC-LAN value (0x0F).

It emits pcap with linktype 162 (DLT_USER15), the payload layout the
avclan_plugin.lua dissector expects -- byte-for-byte compatible with
avclan-pcap.py's output:

    [is_unicast, ctrl_hi, ctrl_lo, periph_hi, periph_lo, control, length, data...]
"""

import argparse
import os
import re
import struct
import sys
from datetime import datetime, timedelta

# pcap on-the-wire format -- mirrors avclan-pcap.py exactly:
# nanosecond magic, snaplen 64, linktype 162 (DLT_USER15 / wtap.USER15).
PCAP_MAGIC_NS = 0xA1B23C4D
SNAPLEN = 64
LINKTYPE_USER15 = 162

PCAP_GLOBAL_HEADER = struct.pack(
    "<IHHiIII",
    PCAP_MAGIC_NS,  # magic (nanosecond resolution, little-endian)
    2,              # version major
    4,              # version minor
    0,              # thiszone
    0,              # sigfigs
    SNAPLEN,        # snaplen
    LINKTYPE_USER15,
)

# AVC-LAN broadcast destination addresses -- a frame to anything else is unicast.
BROADCAST_ADDRS = {0x1FF, 0xFFF}
# This format omits the control field; synthesize the standard AVC-LAN value.
STANDARD_CONTROL = 0x0F

LINE_RE = re.compile(
    r"^\[\s*(\d+):(\d+):(\d+)\.(\d+)\s*\]\s*([0-9A-Fa-f]+)\s*$"
)


def write_global_header(out):
    out.write(PCAP_GLOBAL_HEADER)


def write_record(out, ts_ns, payload):
    sec, nsec = divmod(ts_ns, 1_000_000_000)
    out.write(struct.pack("<IIII", sec, nsec, len(payload), len(payload)))
    out.write(payload)


def parse_line(line, base_date):
    """Parse one log line -> (ts_ns, payload) or None if not a frame."""
    m = LINE_RE.match(line)
    if not m:
        return None
    h, mn, s, frac, hx = m.groups()

    # Header is controller(3) + peripheral(3) + length(2) = 8 nibbles, no control.
    if len(hx) < 8:
        return None
    length = int(hx[6:8], 16)
    if len(hx) != 8 + 2 * length:
        return None

    controller = int(hx[0:3], 16)
    peripheral = int(hx[3:6], 16)
    control = STANDARD_CONTROL  # not present in the log; synthesize the standard
    is_unicast = 0 if peripheral in BROADCAST_ADDRS else 1
    data = bytes.fromhex(hx[8:])

    payload = bytes([
        is_unicast,
        (controller >> 8) & 0xFF, controller & 0xFF,
        (peripheral >> 8) & 0xFF, peripheral & 0xFF,
        control & 0xFF,
        length & 0xFF,
    ]) + data

    micros = int(frac.ljust(6, "0")[:6])
    dt = datetime(base_date.year, base_date.month, base_date.day,
                  int(h), int(mn), int(s))
    return dt, micros, payload


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("infile", nargs="?",
                        default=os.path.join(here, "msgdumps", "log"),
                        help="input log file (default: msgdumps/log)")
    parser.add_argument("-o", "--output",
                        help="output pcap (default: <infile>.pcap)")
    args = parser.parse_args()

    output = args.output or (args.infile + ".pcap")

    # Lines carry only wall-clock time; take the date from the log's mtime.
    try:
        base_date = datetime.fromtimestamp(os.path.getmtime(args.infile))
    except OSError:
        base_date = datetime.now()

    count = 0
    skipped = 0
    prev_tod = None  # previous (h,m,s,micros) for midnight-rollover detection
    day_offset = timedelta(0)

    with open(args.infile, "r") as f, open(output, "wb") as out:
        write_global_header(out)
        for lineno, line in enumerate(f, 1):
            if not line.strip():
                continue
            parsed = parse_line(line, base_date)
            if parsed is None:
                skipped += 1
                print("skip: malformed line %d: %r" % (lineno, line.rstrip()),
                      file=sys.stderr)
                continue
            dt, micros, payload = parsed
            tod = (dt.hour, dt.minute, dt.second, micros)
            if prev_tod is not None and tod < prev_tod:
                day_offset += timedelta(days=1)  # wall clock wrapped past midnight
            prev_tod = tod
            dt = dt + day_offset
            ts_ns = int(dt.timestamp()) * 1_000_000_000 + micros * 1000
            write_record(out, ts_ns, payload)
            count += 1

    print("wrote %d frames to %s%s" % (
        count, output,
        " (%d skipped)" % skipped if skipped else ""), file=sys.stderr)


if __name__ == "__main__":
    main()
