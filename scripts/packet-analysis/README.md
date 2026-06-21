# Packet analysis with Wireshark

- Install the Lua IEBUS/AVCLAN packet dissector to the Wireshark Lua Plugins folder
    - Look in Help > About Wireshark dialog, Folders tab, "Personal Lua Plugins". It was `~/.local/lib/wireshark/plugins` for me, but it didn't exist until I manually created it.
    - Copy or link `avclan_plugin.lua` to the Wireshark Lua Pluginds folder
        - Linking is more convenient if modifying/developing the dissector
          `ln -s $(pwd)/avclan_plugin.lua ~/.local/lib/wireshark/plugins/avclan_plugin.lua`
- The bridge script ([extcap/avclan-pcap.py](extcap/avclan-pcap.py)) needs Python 3 and, for live capture only, [pyserial](https://pypi.org/project/pyserial/). Install pyserial so the `python3` that Wireshark runs can import it (e.g. `pip install pyserial`, or your distro's `python3-serial`). The `--convert` mode and the Wireshark interface listing work without it. (Alternatively, run the script via [uv](https://docs.astral.sh/uv/) — `uv run --script extcap/avclan-pcap.py …` — which resolves the inline PEP 723 dependency automatically; but the GUI extcap uses plain `python3`, so the install above is what makes capture work there.)

## Live capture (Wireshark GUI)

The easiest way to stream packets is as an [extcap](https://www.wireshark.org/docs/man-pages/extcap.html) interface, which appears right in Wireshark's capture list:

- Make the script executable and link it into Wireshark's **Personal Extcap path** (Help > About Wireshark, Folders tab — alongside the Lua plugin folder above):

      chmod +x extcap/avclan-pcap.py
      mkdir ~/.local/lib/wireshark/extcap
      ln -s "$(pwd)/extcap/avclan-pcap.py" ~/.local/lib/wireshark/extcap/avclan-pcap.py

- Restart Wireshark. **AVC-LAN (serial)** now shows up in the interface list. The script autodetects a USB-UART adapter; to override, open the interface's gear/options and set the serial port. Click the shark fin to start. Wireshark owns the process, so stopping the capture (or unplugging the adapter) shuts the bridge down cleanly.

## Live capture (command line)

If you prefer the pipe, the same script works as a plain producer:

    ./extcap/avclan-pcap.py --port /dev/ttyUSB0 | wireshark -k -i -

Omit `--port` (or pass `--port auto`) to autodetect the adapter.

## Converting a text log to pcap

The Mockingboard's text-mode REPL output (e.g. [msgdumps/myfile.txt](msgdumps/myfile.txt)) can be turned into a capture file offline:

    ./extcap/avclan-pcap.py --convert msgdumps/myfile.txt -o out.pcap

Use a `.pcapng` extension to get pcapng instead (requires `editcap`, which ships with Wireshark). With the Lua dissector installed, the result is recognized and dissected as IEBUS/AVCLAN packets.
