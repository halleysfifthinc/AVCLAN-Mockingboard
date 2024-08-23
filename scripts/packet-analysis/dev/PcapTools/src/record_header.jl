struct RecordHeader
    ts_sec::UInt32
    ts_usec::UInt32
    incl_len::UInt32
    orig_len::UInt32
end

function Base.bswap(x::RecordHeader)
    RecordHeader(
        bswap(x.ts_sec),
        bswap(x.ts_usec),
        bswap(x.incl_len),
        bswap(x.orig_len))
end
