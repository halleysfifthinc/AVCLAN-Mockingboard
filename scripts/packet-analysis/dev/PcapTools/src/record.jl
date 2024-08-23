"""
Record of pcap data
"""
struct PcapRecord
    header::RecordHeader
    timestamp::UnixTime
    underlying_data::Vector{UInt8}
    record_offset::Int
end

@inline function record_field_(x::PcapRecord, ::Val{:data})
    offset = getfield(x, :record_offset) + sizeof(RecordHeader)
    len = Int(getfield(x, :header).incl_len)
    UnsafeArray{UInt8, 1}(pointer(getfield(x, :underlying_data)) + offset, (len,))
end

@inline function record_field_(x::PcapRecord, ::Val{:raw})
    offset = getfield(x, :record_offset)
    len = sizeof(RecordHeader) + getfield(x, :header).incl_len
    UnsafeArray{UInt8, 1}(pointer(getfield(x, :underlying_data)) + offset, (len,))
end

@inline record_field_(x::PcapRecord, ::Val{f}) where {f} = getfield(x, f) 

@inline Base.getproperty(x::PcapRecord, f::Symbol) = record_field_(x, Val(f))
