strip_nothing_(::Type{Union{Nothing, T}}) where T = T
strip_nothing_(::Type{T}) where T = T

progress_noop_(n) = nothing

mutable struct SplitCapOutput{S}
    work_buffer::Vector{UInt8}
    complete_buffers::Channel{Vector{UInt8}}
    stream::S
end

# Since Julia (as of 1.5) doesn't support task migration,
# continue with new task after each buffer write, to rebalance across threads
function write_one_and_continue_(output::SplitCapOutput, free_buffers::Channel, pending::Threads.Atomic{Int})
    i = iterate(output.complete_buffers)
    if i === nothing
        Threads.atomic_sub!(pending, 1)
        return nothing
    end
    b, _ = i
    write(output.stream, b)
    empty!(b)
    put!(free_buffers, b)
    Threads.@spawn write_one_and_continue_($output, $free_buffers, $pending)
end

function splitcap(
    ::Type{KeyType},
    ::Type{StreamType},
    reader::PcapReader,
    record2key,
    key2stream,
    progress_callback = progress_noop_;
    own_streams::Bool = true
) where {KeyType, StreamType}
    buffer_size = 1024 * 1024 * 2
    max_pending_buffers = 4
    outputs = Dict{KeyType, SplitCapOutput{StreamType}}()
    free_buffers = Channel{Vector{UInt8}}(Inf)
    n = 0
    pending = Threads.Atomic{Int}(0)
    try
        while !eof(reader)
            record = read(reader)
            dst = record2key(record)
            if dst isa KeyType
                output = get!(outputs, dst) do
                    stream = key2stream(dst)
                    buffer = sizehint!(UInt8[], buffer_size + 1500)
                    own_streams && append!(buffer, reader.raw_header)
                    output = SplitCapOutput{StreamType}(
                        buffer,
                        Channel{Vector{UInt8}}(max_pending_buffers),
                        stream)
                    Threads.atomic_add!(pending, 1)
                    Threads.@spawn write_one_and_continue_($output, $free_buffers, $pending)
                    output
                end
                append!(output.work_buffer, record.raw)
                if length(output.work_buffer) >= buffer_size
                    put!(output.complete_buffers, output.work_buffer)
                    if isready(free_buffers)
                        output.work_buffer = take!(free_buffers)
                    else
                        output.work_buffer = sizehint!(UInt8[], buffer_size + 1500)
                    end
                end
            end
            n += 1
            progress_callback(n)
            GC.safepoint()
        end
        for output in values(outputs)
            if !isempty(output.work_buffer)
                put!(output.complete_buffers, output.work_buffer)
            end
            close(output.complete_buffers)
        end
        while pending[] != 0
            sleep(0.1)
        end
    finally
        if own_streams
            for output in values(outputs)
                close(output.stream)
            end
        end
    end
    nothing
end

function splitcap(
    reader::PcapReader,
    record2key,
    key2stream,
    progress_callback = progress_noop_;
    kwargs...
)
    KeyType = strip_nothing_(Core.Compiler.return_type(record2key, Tuple{PcapRecord}))
    StreamType = Core.Compiler.return_type(key2stream, Tuple{KeyType})
    splitcap(KeyType, StreamType, reader, record2key, key2stream, progress_callback; kwargs...)
end
