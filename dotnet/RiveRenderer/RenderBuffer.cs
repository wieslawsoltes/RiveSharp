using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace RiveRenderer;

public sealed class RenderBuffer : IDisposable
{
    private readonly BufferHandleSafe _handle;
    private readonly BufferType _type;
    private readonly BufferFlags _flags;
    private readonly nuint _size;
    private bool _disposed;

    internal RenderBuffer(BufferHandleSafe handle, BufferType type, BufferFlags flags, nuint size)
    {
        _handle = handle;
        _type = type;
        _flags = flags;
        _size = size;
    }

    public BufferType Type => _type;
    public BufferFlags Flags => _flags;
    public nuint Size => _size;

    internal NativeBufferHandle DangerousGetHandle() => new() { Handle = _handle.DangerousGetHandle() };

    public void Upload(ReadOnlySpan<byte> data, nuint offset = 0)
    {
        ThrowIfDisposed();
        if (offset > _size)
        {
            throw new ArgumentOutOfRangeException(nameof(offset));
        }

        if ((nuint)data.Length > _size - offset)
        {
            throw new ArgumentOutOfRangeException(nameof(data), "Data span exceeds the target buffer size.");
        }

        if (data.IsEmpty)
        {
            return;
        }

        unsafe
        {
            fixed (byte* ptr = data)
            {
                NativeMethods.Buffer.Upload(DangerousGetHandle(), ptr, (nuint)data.Length, offset)
                    .ThrowIfFailed("Failed to upload data to buffer.");
            }
        }
    }

    public Mapping Map(BufferMapFlags flags = BufferMapFlags.None)
    {
        ThrowIfDisposed();
        var status = NativeMethods.Buffer.Map(DangerousGetHandle(), flags, out var mapping);
        status.ThrowIfFailed("Failed to map buffer.");
        if (mapping.Data == nint.Zero)
        {
            throw new RendererException(RendererStatus.InternalError, "Buffer map returned a null pointer.");
        }

        return new Mapping(this, mapping);
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _handle.Dispose();
    }

    internal void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(RenderBuffer));
        }
    }

    internal void UnmapInternal(in NativeMappedMemory mapping, nuint writtenBytes)
    {
        ThrowIfDisposed();
        var status = NativeMethods.Buffer.Unmap(DangerousGetHandle(), mapping, writtenBytes);
        status.ThrowIfFailed("Failed to unmap buffer.");
    }

    public sealed class Mapping : IDisposable
    {
        private readonly RenderBuffer _buffer;
        private NativeMappedMemory _mapping;
        private bool _disposed;

        internal Mapping(RenderBuffer buffer, NativeMappedMemory mapping)
        {
            _buffer = buffer;
            _mapping = mapping;
        }

        public nuint Length => _mapping.Length;

        public unsafe Span<byte> AsSpan()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(Mapping));
            }

            return new Span<byte>((void*)_mapping.Data, checked((int)_mapping.Length));
        }

        public nint Data
        {
            get
            {
                if (_disposed)
                {
                    throw new ObjectDisposedException(nameof(Mapping));
                }
                return _mapping.Data;
            }
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            _buffer.UnmapInternal(_mapping, _mapping.Length);
            _mapping = default;
            _disposed = true;
        }
    }
}
