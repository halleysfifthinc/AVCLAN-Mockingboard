#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""AVC-LAN Mockingboard -> pcap bridge for Wireshark.

Three modes, one script:

  * extcap  - invoked by Wireshark; appears as the "AVC-LAN (serial)" capture
              interface. Wireshark owns the process lifecycle, so stopping the
              capture (or unplugging the adapter) tears everything down cleanly.
  * pipe    - run by hand: `./avclan-pcap.py --port /dev/ttyUSB0 | wireshark -k -i -`
  * convert - turn a text-mode serial log into a pcap file offline:
              `./avclan-pcap.py --convert msgdumps/myfile.txt -o out.pcap`

The firmware emits frames over serial at 1.2 Mbaud, 8N1, in one of two formats
(selected at the REPL). This script understands both and emits pcap with
linktype 162 (USER15), matching the avclan_plugin.lua dissector.

Discovery (the extcap query phases), `--convert`, and argument handling run on
the standard library alone, so the script keeps a plain `python3` shebang and
Wireshark can always exec it -- Wireshark launches extcaps with a minimal PATH,
which is why a `uv` shebang here would make the interface vanish from the GUI.
The one dependency, `pyserial`, is only needed to actually read the serial port:
capture fails with a clear message if it is missing. Install it normally (so the
`python3` invocation Wireshark uses can find it), or run the script via
`uv run --script` to have uv resolve the inline dependency above.
"""

import argparse
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timedelta

# ---------------------------------------------------------------------------
# pcap on-the-wire format. Mirrors the old Julia PcapTools output exactly:
# nanosecond magic, snaplen 64, linktype 162 (DLT_USER15 / wtap.USER15).
# ---------------------------------------------------------------------------
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

# Serial framing bytes emitted by AVCLAN_printframe(..., binary=1).
DLE = 0x10  # start of a binary frame
ETB = 0x17  # end of a binary frame (followed by \r\n)
MAX_DATA_LEN = 32  # AVCLAN payload cap; longer "length" => bad framing

BAUD = 1_200_000

# USB-UART bridge vendor IDs worth preferring during autodetect.
PREFERRED_VIDS = {
    0x0403,  # FTDI
    0x10C4,  # Silicon Labs CP210x
    0x1A86,  # WCH CH340/CH341
    0x067B,  # Prolific PL2303
}


# ---------------------------------------------------------------------------
# pcap writing
# ---------------------------------------------------------------------------
def write_global_header(out):
    out.write(PCAP_GLOBAL_HEADER)
    out.flush()


def write_record(out, ts_ns, payload):
    sec, nsec = divmod(ts_ns, 1_000_000_000)
    out.write(struct.pack("<IIII", sec, nsec, len(payload), len(payload)))
    out.write(payload)
    out.flush()


# ---------------------------------------------------------------------------
# Binary frame extraction (live capture / pipe modes)
#
# Length-based rather than line-based: the firmware does not escape payload
# bytes, so a data byte of 0x0A/0x10/0x17 must not be allowed to split a frame.
# We locate a DLE, read the fixed 7-byte header, trust its `length` field, then
# require an ETB exactly where it should be -- resyncing on any mismatch.
# ---------------------------------------------------------------------------
def extract_frames(buf):
    """Pull complete frame payloads out of `buf`, consuming what it returns.

    Yields the dissector-ready payload for each frame:
    [is_unicast, ctrl_hi, ctrl_lo, periph_hi, periph_lo, control, length, data...]
    Leaves any trailing partial frame in `buf` for the next call.
    """
    frames = []
    pos = 0
    while True:
        dle = buf.find(DLE, pos)
        if dle == -1:
            buf.clear()
            return frames
        # Need DLE + 7 header bytes (the last of which is `length`).
        if len(buf) < dle + 8:
            del buf[:dle]
            return frames
        length = buf[dle + 7]
        if length > MAX_DATA_LEN:
            pos = dle + 1  # spurious DLE; resync past it
            continue
        etb = dle + 8 + length  # index where ETB must sit
        if len(buf) < etb + 1:
            del buf[:dle]
            return frames
        if buf[etb] != ETB:
            pos = dle + 1  # framing mismatch; this DLE was not a real start
            continue
        frames.append(bytes(buf[dle + 1:etb]))  # 7 header bytes + data
        pos = etb + 1
        while pos < len(buf) and buf[pos] in (0x0D, 0x0A):
            pos += 1  # swallow the trailing \r\n
        del buf[:pos]
        pos = 0


# ---------------------------------------------------------------------------
# Serial helpers (lazy pyserial import)
# ---------------------------------------------------------------------------
def detect_port():
    """Best-guess serial device, or None. Prefers known USB-UART bridges.

    Uses pyserial's enumeration when available, but falls back to a stdlib
    glob so the extcap config dialog still offers a sensible default before
    pyserial has been pulled in.
    """
    try:
        from serial.tools import list_ports
    except ImportError:
        import glob
        cands = sorted(glob.glob("/dev/ttyUSB*")) + sorted(glob.glob("/dev/ttyACM*"))
        return cands[0] if cands else None

    ports = list(list_ports.comports())
    if not ports:
        return None

    def score(p):
        s = 0
        if p.vid in PREFERRED_VIDS:
            s += 10
        dev = p.device or ""
        if "ttyUSB" in dev:
            s += 3
        elif "ttyACM" in dev:
            s += 2
        return s

    ports.sort(key=score, reverse=True)
    return ports[0].device


def ensure_pyserial():
    """Fail with a clear message if pyserial (capture-only) is unavailable."""
    try:
        import serial  # noqa: F401
    except ImportError:
        sys.exit("error: capture requires pyserial, which is not installed.\n"
                 "Install it (e.g. `pip install pyserial`), or run this script via "
                 "`uv run --script` to pull in the inline dependency.")


def resolve_port(requested):
    """Map a requested port (possibly None/"auto") to a concrete device."""
    if requested and requested != "auto":
        return requested
    port = detect_port()
    if not port:
        sys.exit("error: no serial port given and none could be autodetected "
                 "(pass --port /dev/ttyUSBx)")
    return port


def open_serial(port):
    import serial  # lazy: only the serial modes need it

    # Flow control fully off: the binary payload can legitimately contain
    # 0x11 (XON) / 0x13 (XOFF), which a flow-controlled link would eat.
    return serial.Serial(
        port,
        BAUD,
        timeout=0.2,  # short, so signal flags get checked promptly
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    )


# ---------------------------------------------------------------------------
# Capture (extcap + pipe)
# ---------------------------------------------------------------------------
_stop = False


def _request_stop(signum, frame):
    global _stop
    _stop = True


def run_capture(out, port):
    """Stream frames from the serial port to `out` until told to stop."""
    ensure_pyserial()  # re-execs under uv if pyserial is missing
    import serial  # for SerialException

    port = resolve_port(port)
    try:
        ser = open_serial(port)
    except serial.SerialException as e:
        sys.exit("error: could not open %s: %s" % (port, e))

    # Put the firmware into binary-frame mode (REPL 'X').
    try:
        ser.write(b"X")
    except serial.SerialException:
        pass

    buf = bytearray()
    try:
        write_global_header(out)
        while not _stop:
            try:
                chunk = ser.read(ser.in_waiting or 1)
            except serial.SerialException:
                break  # adapter went away -> stop cleanly
            if not chunk:
                continue
            buf.extend(chunk)
            for payload in extract_frames(buf):
                write_record(out, time.time_ns(), payload)
    except (BrokenPipeError, OSError):
        pass  # Wireshark closed the fifo/pipe -> we're done
    finally:
        ser.close()


# ---------------------------------------------------------------------------
# Convert (offline text log -> pcap), replacing Julia avclan_text_to_pcap
# ---------------------------------------------------------------------------
TIME_RE = re.compile(r"^(\d{1,2}):(\d{2}):(\d{2})\.(\d+)$")


def parse_text_line(line, base_date):
    """Parse one text-mode log line.

    Returns (ts_ns_or_None, payload) or None if the line is not a frame.
    Line format (whitespace separated):
        [HH:MM:SS.mmm] is_unicast controller peripheral control length data...
    with the numeric fields in C-style notation (0x.. or plain decimal).
    """
    tokens = line.split()
    if len(tokens) < 5:
        return None

    ts_ns = None
    m = TIME_RE.match(tokens[0])
    if m:
        h, mn, s, frac = m.groups()
        micros = int(frac.ljust(6, "0")[:6])
        try:
            dt = datetime(base_date.year, base_date.month, base_date.day,
                          int(h), int(mn), int(s))
        except ValueError:
            return None
        ts_ns = int(dt.timestamp()) * 1_000_000_000 + micros * 1000
        tokens = tokens[1:]

    if len(tokens) < 5:
        return None
    try:
        is_unicast = int(tokens[0], 0)
        controller = int(tokens[1], 0)
        peripheral = int(tokens[2], 0)
        control = int(tokens[3], 0)
        length = int(tokens[4], 0)
    except ValueError:
        return None

    data_tokens = tokens[5:]
    if length > MAX_DATA_LEN or len(data_tokens) != length:
        return None
    try:
        data = bytes(int(d, 0) & 0xFF for d in data_tokens)
    except ValueError:
        return None

    payload = bytes([
        is_unicast & 0xFF,
        (controller >> 8) & 0xFF, controller & 0xFF,
        (peripheral >> 8) & 0xFF, peripheral & 0xFF,
        control & 0xFF,
        length & 0xFF,
    ]) + data
    return ts_ns, payload


def run_convert(infile, output):
    # Date comes from the log's mtime (the lines only carry a wall-clock time);
    # falls back to today if that is unavailable.
    try:
        base_date = datetime.fromtimestamp(os.path.getmtime(infile))
    except OSError:
        base_date = datetime.now()

    want_pcapng = bool(output) and output.endswith(".pcapng")
    tmp_path = None
    if want_pcapng:
        fd, tmp_path = tempfile.mkstemp(suffix=".pcap")
        out = os.fdopen(fd, "wb")
        close_out = True
    elif output:
        out = open(output, "wb")
        close_out = True
    else:
        out = sys.stdout.buffer
        close_out = False

    last_ts = None
    count = 0
    try:
        write_global_header(out)
        with open(infile, "r") as f:
            for line in f:
                parsed = parse_text_line(line, base_date)
                if parsed is None:
                    continue
                ts_ns, payload = parsed
                if ts_ns is None:
                    # No timestamp on this line; nudge past the previous one so
                    # ordering is preserved (mirrors the old Julia behaviour).
                    base = last_ts if last_ts is not None else time.time_ns()
                    ts_ns = base + 1000
                write_record(out, ts_ns, payload)
                last_ts = ts_ns
                count += 1
    finally:
        if close_out:
            out.close()

    if want_pcapng:
        editcap = shutil.which("editcap")
        if editcap:
            subprocess.run([editcap, "-F", "pcapng", tmp_path, output], check=True)
            os.unlink(tmp_path)
        else:
            fallback = output[: -len(".pcapng")] + ".pcap"
            shutil.move(tmp_path, fallback)
            print("warning: editcap not found; wrote pcap to %s instead of %s"
                  % (fallback, output), file=sys.stderr)
            output = fallback

    if output:
        print("wrote %d frames to %s" % (count, output), file=sys.stderr)


# ---------------------------------------------------------------------------
# extcap argument phases
# ---------------------------------------------------------------------------
def extcap_interfaces():
    print("extcap {version=1.0}{help=https://github.com/}"
          "{display=AVC-LAN Mockingboard}")
    print("interface {value=avclan}{display=AVC-LAN (serial)}")


def extcap_dlts():
    print("dlt {number=%d}{name=USER15}{display=AVC-LAN / IEBus}" % LINKTYPE_USER15)


def extcap_config():
    default = detect_port() or "auto"
    print('arg {number=0}{call=--port}{display=Serial port}{type=string}'
          '{tooltip=Serial device path, or "auto" to autodetect}'
          '{default=%s}' % default)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(add_help=True)
    # extcap discovery/lifecycle flags (Wireshark-driven)
    parser.add_argument("--extcap-interfaces", action="store_true")
    parser.add_argument("--extcap-dlts", action="store_true")
    parser.add_argument("--extcap-config", action="store_true")
    parser.add_argument("--extcap-interface")
    parser.add_argument("--extcap-version")
    parser.add_argument("--capture", action="store_true")
    parser.add_argument("--fifo")
    parser.add_argument("--extcap-control-in")
    parser.add_argument("--extcap-control-out")
    # shared / CLI
    parser.add_argument("--port", help='serial device, or "auto"')
    parser.add_argument("--convert", metavar="LOGFILE",
                        help="convert a text-mode serial log to pcap")
    parser.add_argument("-o", "--output",
                        help="output file for --convert (.pcap or .pcapng); "
                             "stdout if omitted")
    args, _ = parser.parse_known_args()

    if args.extcap_interfaces:
        extcap_interfaces()
        return
    if args.extcap_dlts:
        extcap_dlts()
        return
    if args.extcap_config:
        extcap_config()
        return
    if args.convert:
        run_convert(args.convert, args.output)
        return

    signal.signal(signal.SIGINT, _request_stop)
    signal.signal(signal.SIGTERM, _request_stop)

    if args.capture:
        if not args.fifo:
            sys.exit("error: --capture requires --fifo")
        with open(args.fifo, "wb") as fifo:
            run_capture(fifo, args.port)
        return

    # No mode flags: behave as a plain pipe into `wireshark -k -i -`.
    run_capture(sys.stdout.buffer, args.port)


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:
        # Reader (Wireshark) went away; exit quietly without a traceback.
        try:
            sys.stdout.close()
        except Exception:
            pass
        os._exit(0)
