include("AVCLANPipe.jl")

using .AVCLANPipe, PcapTools, Dates, UnixTimes, LibSerialPort

serial_port="/dev/ttyUSB0"
baud=1200000

pcapstream = PcapStreamWriter(stdout; snaplen=64, linktype=162)
serial = LibSerialPort.open(serial_port, baud)
set_flow_control(serial) # Disable flow-control (stops it from eating raw byte 0x11)

write(serial, 'X')
buf = IOBuffer()

function quit(serialio=serial)
    close(serialio)
    exit()
end

while true
    iswritable(stdout) || isopen(stdout) || quit()
    if bytesavailable(serial) > 0
        t = UnixTime(now())
        line = readline(serial)
        # allthebytes = Vector{UInt8}(undef, bytesavailable(serial))
        # readbytes!(serial, allthebytes)
        # write(buf, allthebytes)
        # l = findfirst(==('\n'), buf.data)
        # if !isnothing(l)
            # println(stderr, "fucku")
            # bytes = Vector{UInt8}(undef, l)
            # seekstart(buf)
            # readbytes!(buf, bytes)
            # seekend(buf)
             bytes = Vector{UInt8}(line)
            if bytes[1] == 0x10 && bytes[end] == 0x17
                write(pcapstream, t, bytes[2:end-1])
            else
                @error line
            end
        # end
        # println(stderr, "wtf")
    end
    yield()
end
