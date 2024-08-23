"""
Reads pcap data from a stream.
"""
mutable struct PcapStreamReader{Src <: IO} <: PcapReader
    src::Src
    raw_header::Vector{UInt8}
    header::PcapHeader
    usec_mul::Int64
    bswapped::Bool
    record_buffer::Vector{UInt8}

    @doc """
        PcapStreamReader(src::IO)

    Create reader over `src`. Will read and process pcap header,
    and yield records through `read(::PcapStreamReader)`.
    """
    function PcapStreamReader(src::Src) where {Src <: IO}
        raw_header = read(src, sizeof(PcapHeader))
        length(raw_header) != sizeof(PcapHeader) && throw(EOFError())
        h = GC.@preserve raw_header unsafe_load(Ptr{PcapHeader}(pointer(raw_header)))
        header, bswapped, nanotime = process_header(h)
        new{Src}(src, raw_header, header, nanotime ? 1 : 1000, bswapped, zeros(UInt8, 9000 + sizeof(RecordHeader)))
    end
end

"""
    PcapStreamReader(path)

Open file at `path` and create PcapStreamReader over its content.
"""
PcapStreamReader(path::AbstractString) = PcapStreamReader(open(path))

Base.close(x::PcapStreamReader) = close(x.src)
Base.position(x::PcapStreamReader) = position(x.src)
Base.seek(x::PcapStreamReader, pos) = seek(x.src, pos)
Base.mark(x::PcapStreamReader) = mark(x.src)
Base.unmark(x::PcapStreamReader) = unmark(x.src)
Base.ismarked(x::PcapStreamReader) = ismarked(x.src)
Base.reset(x::PcapStreamReader) = reset(x.src)
Base.eof(x::PcapStreamReader) = eof(x.src)

"""
    read(x::PcapStreamReader) -> PcapRecord

Read one record from pcap data. Record is valid until next read().
Throws `EOFError` if no more data available.
"""
function Base.read(x::PcapStreamReader)
    p = pointer(x.record_buffer)
    GC.@preserve x begin
        unsafe_read(x.src, p, sizeof(RecordHeader))
        h = unsafe_load(Ptr{RecordHeader}(p))
        if x.bswapped
            h = bswap(h)
        end
        unsafe_read(x.src, p + sizeof(RecordHeader), h.incl_len)
    end
    t1 = (h.ts_sec + x.header.thiszone) * 1_000_000_000
    t2 = Int64(h.ts_usec) * x.usec_mul
    t = UnixTime(Dates.UTInstant(Nanosecond(t1 + t2)))
    PcapRecord(h, t, x.record_buffer, 0)
end
