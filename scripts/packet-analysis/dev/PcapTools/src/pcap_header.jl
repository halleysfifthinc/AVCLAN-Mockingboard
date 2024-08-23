# NOTE: Not using @enum because it craps out when displaying unknown values
const LINKTYPE_NULL = UInt32(0)
const LINKTYPE_ETHERNET = UInt32(1)

struct PcapHeader
    magic::UInt32
    version_major::UInt16
    version_minor::UInt16
    thiszone::Int32
    sigfigs::UInt32
    snaplen::UInt32
    linktype::UInt32
end

function Base.bswap(x::PcapHeader)
    PcapHeader(
        bswap(x.magic),
        bswap(x.version_major),
        bswap(x.version_minor),
        bswap(x.thiszone),
        bswap(x.sigfigs),
        bswap(x.snaplen),
        bswap(x.linktype))
end

function process_header(x::PcapHeader)
    if x.magic == 0xa1b2c3d4
        bswapped = false
        nanotime = false
    elseif x.magic == 0xd4c3b2a1
        bswapped = true
        nanotime = false
    elseif x.magic == 0xa1b23c4d
        bswapped = false
        nanotime = true
    elseif x.magic == 0x4d3cb2a1
        bswapped = true
        nanotime = true
    else
        throw(ArgumentError("Invalid pcap header"))
    end
    if bswapped
        x = bswap(x)
    end
    x, bswapped, nanotime
end
