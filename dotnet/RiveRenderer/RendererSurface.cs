using System;

namespace RiveRenderer;

public sealed class RendererSurface : IDisposable
{
    private readonly RendererDevice _device;
    private readonly RendererContext _context;
    private readonly SurfaceHandleSafe _handle;
    private bool _disposed;

    internal RendererSurface(RendererDevice device, RendererContext context, SurfaceHandleSafe handle)
    {
        _device = device;
        _context = context;
        _handle = handle;
    }

    internal NativeSurfaceHandle DangerousGetHandle() => new() { Handle = _handle.DangerousGetHandle() };

    public (uint Width, uint Height) Size
    {
        get
        {
            ThrowIfDisposed();
            var status = NativeMethods.Surface.GetSize(DangerousGetHandle(), out var width, out var height);
            status.ThrowIfFailed("Failed to query surface size.");
            _context.UpdateSizeFromSurface(width, height);
            return (width, height);
        }
    }

    public void Resize(uint width, uint height)
    {
        ThrowIfDisposed();
        var status = NativeMethods.Surface.Resize(DangerousGetHandle(), width, height);
        status.ThrowIfFailed("Failed to resize surface.");
        _context.UpdateSizeFromSurface(width, height);
    }

    public void Present(uint presentInterval = 0, RendererPresentFlags flags = RendererPresentFlags.None)
    {
        ThrowIfDisposed();
        var status = NativeMethods.Surface.Present(DangerousGetHandle(), presentInterval, flags);
        status.ThrowIfFailed("Surface presentation failed.");
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

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(RendererSurface));
        }
    }
}
