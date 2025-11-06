using System;

namespace RiveRenderer;

public sealed class RendererFence : IDisposable
{
    private readonly RendererDevice _device;
    private readonly FenceHandleSafe _handle;
    private bool _disposed;

    internal RendererFence(RendererDevice device, FenceHandleSafe handle)
    {
        _device = device;
        _handle = handle;
    }

    internal NativeFenceHandle DangerousGetHandle() => new() { Handle = _handle.DangerousGetHandle() };

    internal void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(RendererFence));
        }
    }

    public ulong GetCompletedValue()
    {
        ThrowIfDisposed();
        NativeMethods.Fence.GetCompletedValue(DangerousGetHandle(), out var value)
            .ThrowIfFailed("Failed to query fence value.");
        return value;
    }

    public void Wait(ulong value, ulong timeoutMilliseconds = ulong.MaxValue)
    {
        ThrowIfDisposed();
        NativeMethods.Fence.Wait(DangerousGetHandle(), value, timeoutMilliseconds)
            .ThrowIfFailed("Fence wait failed.");
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
}
