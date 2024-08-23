# Packet analysis with Wireshark

- Install the Lua IEBUS/AVCLAN packet dissector to the Wireshark Lua Plugins folder
    - Look in Help > About Wireshark dialog, Folders tab, "Personal Lua Plugins". It was `~/.local/lib/wireshark/plugins` for me, but it didn't exist until I manually created it.
    - Copy or link `avclan_plugin.lua` to the Wireshark Lua Pluginds folder
        - Linking is more convenient if modifying/developing the dissector
          `ln -s $(pwd)/avclan_plugin.lua ~/.local/lib/wireshark/plugins/avclan_plugin.lua`
- Install Julia and needed packages (I recommend using [juliaup](https://github.com/JuliaLang/juliaup) or an official binary from the Julialang website. Binaries from your distribution are typically out of date and/or built incorrectly.)
    - From this folder, start Julia with the local project environment using `julia --project=@.`
    - Run `] instantiate` to install the necessary Julia packages
- Run `julia src/pipepackets.jl | wireshark -k -i -` to pipe packets logged by the Mockingboard over serial into Wireshark. If the Lua dissector is installed correctly, the packets should be correctly recognized and dissected as IEBUS/AVCLAN packets