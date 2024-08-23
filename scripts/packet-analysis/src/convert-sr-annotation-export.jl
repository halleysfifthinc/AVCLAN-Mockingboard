using AVCLANPipe, PcapTools, Dates, UnixTimes

file = ARGS[1]
outfile = ARGS[2]

function convert_fields_to_pcap(file, outfile)

    io = open(file, "r")
    outio = PcapStreamWriter(outfile; snaplen=64, linktype = 162)

    packet = AVCLANframe()
    packetcomplete = false
    local t::UnixTime
    i = 1

    for line in readlines(io)
        m = match(r"(\d+)-(\d+) IEBus: Raw Fields: (?<field>\w+)(:? ((?<hex>0x[0-9a-f]{2,3})|WRITE_DATA|Length: (?<length>\d+)))?",
            line)

        if m[:field] == "Broadcast"
            packet.broadcast = true
            t = UnixTime(1970,1,1,0,0,0,0, something(tryparse(Int, m[1]), 0))
        elseif m[:field] == "Unicast"
            packet.broadcast = false
            t = UnixTime(1970,1,1,0,0,0,0, something(tryparse(Int, m[1]), 0))
        elseif m[:field] == "Master"
            packet.controller_addr = tryparse(UInt16, m[:hex])
        elseif m[:field] == "Slave"
            packet.peripheral_addr = tryparse(UInt16, m[:hex])
        elseif m[:field] == "Data"
            if !isnothing(m[:length])
                packet.length = convert(UInt8, tryparse(Int, m[:length]))
            else
                data = collect(packet.data)
                data[i] = tryparse(UInt8, m[:hex])
                packet.data = ntuple(n -> data[n], 32)
                i += 1
            end
        end

        if i > packet.length
            packetcomplete = true
        end

        if packetcomplete
            packetcomplete = false
            i = 1
            write(outio, t, tobytes(packet))
        end
    end

    close(io)
    close(outio)

end
