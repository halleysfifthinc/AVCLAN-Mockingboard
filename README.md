# AVCLAN Mockingboard

<img src="./hardware/pcbv1.png" width=500\>

This project adds an aux in to the stock head unit of compatible Toyota vehicles by emulating an external CD changer. The Mockingboard communicates with the head unit over the AVC-LAN bus, Toyota's name for their messaging protocol over the NEC (now Renesas) IEBus. Also included is a Wireshark dissector for IEBus and AVC-LAN. AVC-LAN is sparsely documented publicly; this dissector collects what's known from prior art alongside my own findings.

# Features:

- Select/switch between internal CD player and mockingboard by pressing the "CD" button (repeated presses toggle between internal and external CD players)
- Generally complete handling of physical interface (track next/prev, fast-forward/rewind, track/disk repeat and random; "scan" button and change disc buttons are currently unimplemented for lack of use/purpose)
- Play/pause, next/prev control of connected phones via headset functions (mimicing the button press on a wired headset; requires phone support and connection using a TRRS aux cable)

## Future plans (Mockingboard v2)

- Bluetooth functionality
  - "Scan" button repurposed to enter bluetooth pairing mode
  - Button actions (play/pause, track skip, ff/rw, \[disc\] repeat/shuffle) mapped to AVRCP
  - Song info/status (time, etc) relayed to head-unit (i.e. time display matches actual song/audio time)
- Redesigned hardware based on RPi Pico 2W (RP2350 + Bluetooth)
  - PIO used to implement:
    - AVCLAN comms
    - I2S audio
  - Dual audio input options (aux in, bluetooth)
    - Switched audio jack to sense aux plug presence
  - Audio codec with differential output (better rejection of electrical noise, e.g. from adjacent AVCLAN bus lines)
  - Switching regulator (easier assembly)
  - CAN transceiver
    - TX/RX connected to different diodes (or different pads of a single dual/multicolor LED package) for observability

# Helpful links/prior art:

- IEBus/AVCLAN decoder and message dumps (inc. a CD changer) for Sigrok
    - https://github.com/sigrokproject/sigrok-test/pull/22
    - https://github.com/sigrokproject/sigrok-dumps/pull/43
    - https://github.com/sigrokproject/libsigrokdecode/pull/106
- https://web.archive.org/web/20230319000356/http://softservice.com.pl/corolla/avc/avclan.php
- https://web.archive.org/web/20040617005106/http://www.interfacebus.com/Design_Connector_IEbus.html
- https://web.archive.org/web/2/https://old.pinouts.ru/Car-Stereo-Toyota-Lexus/Toyota_1990-2002_CD_Chang_pinout.shtml
- https://web.archive.org/web/20240519043021/https://pop.fsck.pl/hardware/toyota-corolla.html
- https://github.com/GadgetNutt/AVC-LAN-Module-Builder

# Firmware

### Building

#### With VS Code:

1. Install VS Code and Docker
    - Required extensions: Dev Containers
2. Open repo in VS code and wait for notification asking to re-open in dev container.
    - Wait for container to build and extensions to install
3. Select CMake build configuration
4. Start developing!

#### Natively/without VS Code Dev Containers
1. Install avr-gcc >= v13.1, binutils >= v2.39, cmake >= v3.24
2. Configure cmake with a hardware-target preset, `cmake --list-presets` to discover options
    - Trigger builds with e.g. `cmake --build out/build/attiny3216`
3. Start developing!

# Hardware

## First generation

See [here](hardware/mockingboard-v1/README.md) for details.

## Second generation

Under development!

# Protocol reverse-engineering

The "scripts/packet-analysis" folder contains a [Wireshark](https://www.wireshark.org/) [Lua plugin](https://www.wireshark.org/docs/wsdg_html_chunked/wsluarm.html) that defines a dissector for IEBUS and AVC-LAN messages.

The dissector's `known_devices` / `known_actions` tables are the source of truth
for AVC-LAN device and action names. `scripts/sync_avclan_enums.py` reconciles
the firmware's C++ `Device` / `Action` enums (and every reference under `src/`)
against them, matching by value so renamed values are propagated. It runs as a
pre-commit hook — the top-level CMake configure step points `core.hooksPath` at
`.githooks/`, so `cmake --preset …` activates it automatically (opt out with
`-DINSTALL_GIT_HOOKS=OFF`; `git commit --no-verify` bypasses one commit). The
hook only reports drift; run `scripts/sync_avclan_enums.py --fix` to apply, and
`--lint` to check that the dissector's own names are valid identifiers.

# License

The firmware for this project is licensed under the [GNU GPLv3](https://www.gnu.org/licenses/gpl-3.0.html). The AVR-Attiny3216 hardware target depends on a vendored [MIT](https://spdx.org/licenses/MIT) licensed [UART library](https://github.com/jnk0le/AVR-UART-lib), and the [MPL-2.0](https://www.mozilla.org/MPL/2.0/) licensed [avr-libstdcpp](https://github.com/modm-io/avr-libstdcpp/) C++ STL subset.
The hardware is licensed under the [Solderpad Hardware License v2.1](http://solderpad.org/licenses/SHL-2.1/), a
wraparound license to the [Apache License 2.0](https://apache.org/licenses/LICENSE-2.0.txt).


