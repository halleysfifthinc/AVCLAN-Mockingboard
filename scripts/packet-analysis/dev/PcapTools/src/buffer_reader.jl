"""
Reads pcap data from an array of bytes.
"""
mutable struct PcapBufferReader <: PcapReader
    data::Vector{UInt8}
    raw_header::Vector{UInt8}
    header::PcapHeader
    offset::Int64
    mark::Int64
    usec_mul::Int64
    bswapped::Bool

    @doc """
        PcapBufferReader(data::Vector{UInt8})

    Create reader over `data`. Will read and process pcap header,
    and yield records through `read(::PcapBufferReader)`.
    """
    function PcapBufferReader(data::Vector{UInt8})
        length(data) < sizeof(PcapHeader) && throw(EOFError())
        rh = data[1:sizeof(PcapHeader)]
        h = unsafe_load(Ptr{PcapHeader}(pointer(data)))
        h, bswapped, nanotime = process_header(h)
        new(data, rh, h, sizeof(h), -1, nanotime ? 1 : 1000, bswapped)
    end
end

"""
    PcapBufferReader(path::AbstractString)

Memory map file in `path` and create PcapBufferReader over its content.
"""
function PcapBufferReader(path::AbstractString)
    io = open(path)
    data = Mmap.mmap(io)
    PcapBufferReader(data)
end

function Base.close(x::PcapBufferReader)
    x.data = UInt8[]
    x.offset = 0
    nothing
end

Base.length(x::PcapBufferReader) = length(x.data)
Base.position(x::PcapBufferReader) = x.offset; nothing
Base.seek(x::PcapBufferReader, pos) = x.offset = pos; nothing

function Base.mark(x::PcapBufferReader)
    x.mark = x.offset
    x.mark
end

function Base.unmark(x::PcapBufferReader)
    if x.mark >= 0
        x.mark = -1
        true
    else
        false
    end
end

Base.ismarked(x::PcapBufferReader) = x.mark >= 0

function Base.reset(x::PcapBufferReader)
    !ismarked(x) && error("PcapBufferReader not marked")
    x.offset = x.mark
    x.mark = -1
    x.offset
end

Base.eof(x::PcapBufferReader) = (length(x) - x.offset) < sizeof(RecordHeader)

"""
    read(x::PcapBufferReader) -> PcapRecord

Read one record from pcap data.
Throws `EOFError` if no more data available.
"""
@inline function Base.read(x::PcapBufferReader)
    eof(x) && throw(EOFError())
    record_offset = x.offset
    p = pointer(x.data) + record_offset
    GC.@preserve x begin
        h = unsafe_load(Ptr{RecordHeader}(p))
    end
    if x.bswapped
        h = bswap(h)
    end
    t1 = (h.ts_sec + x.header.thiszone) * 1_000_000_000
    t2 = Int64(h.ts_usec) * x.usec_mul
    t = UnixTime(Dates.UTInstant(Nanosecond(t1 + t2)))
    x.offset += sizeof(RecordHeader) + h.incl_len
    x.offset > length(x) && error("Insufficient data in pcap record")
    PcapRecord(h, t, x.data, record_offset)
end
