module AVCLANPipe

using PcapTools, Dates, UnixTimes

export AVCLANframe, avclan_text_to_pcap, tobytes

mutable struct AVCLANframe
    broadcast::Bool
    controller_addr::UInt16
    peripheral_addr::UInt16
    control::UInt8
    length::UInt8
    data::NTuple{32,UInt8}
end

AVCLANframe() = AVCLANframe(false, 0x0000, 0x0000, 0xf, 0x0, ntuple(x -> 0x0, 32))

function Base.tryparse(::Type{AVCLANframe}, str::String)
    vals = split(str)

    broadcast = tryparse(Bool, vals[1])
    isnothing(broadcast) && return nothing

    controller_addr = tryparse(UInt16, vals[2])
    isnothing(controller_addr) && return nothing

    peripheral_addr = tryparse(UInt16, vals[3])
    isnothing(peripheral_addr) && return nothing

    control = tryparse(UInt8, vals[4])
    isnothing(control) && return nothing

    len = tryparse(UInt8, vals[5])
    isnothing(len) && return nothing

    if (length(vals) - 5) != len || len > 32
        return nothing
    end
    _data = tryparse.(UInt8, vals[6:end])
    data = ntuple(i -> checkindex(Bool, axes(_data, 1), i) ? _data[i] : 0x0, 32)

    return AVCLANframe(broadcast, controller_addr, peripheral_addr, control, len, data)
end

function tobytes(frame::AVCLANframe)
    data = Vector{UInt8}(undef, 0)
    push!(data, frame.broadcast)
    append!(data, reverse(reinterpret(reshape, UInt8, [frame.controller_addr])),
            reverse(reinterpret(reshape, UInt8, [frame.peripheral_addr])),
            reinterpret(reshape, UInt8, [frame.control]),
            reinterpret(reshape, UInt8, [frame.length]),
            frame.data[1:frame.length])

    return data
end

function avclan_text_to_pcap(textlog::String, pcap_fn::String)
    pcapstream = PcapStreamWriter(pcap_fn; snaplen=64, linktype = 162)
    t = UnixTime(now())

    for line in eachline(textlog)
        frame = tryparse(AVCLANframe, line)
        t += Microsecond(rand(1:15))
        if !isnothing(frame)
            write(pcapstream, t, tobytes(frame))
        end
    end
    close(pcapstream)

    return pcap_fn
end

end # module AVCLANPipe
