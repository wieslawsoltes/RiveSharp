using System;
using System.Buffers;

namespace RiveRenderer;

public sealed class RendererDevice : IDisposable
{
    private DeviceHandle _handle;
    private bool _disposed;

    private RendererDevice(DeviceHandle handle)
    {
        _handle = handle;
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(RendererDevice));
        }
    }

    public static RendererDevice Create(RendererBackend backend, ushort adapterIndex = 0, RendererDeviceFlags flags = RendererDeviceFlags.None)
    {
        var createInfo = new NativeDeviceCreateInfo
        {
            Backend = backend,
            AdapterIndex = adapterIndex,
            Flags = flags,
        };

        var status = NativeMethods.Device.Create(createInfo, out var nativeHandle);
        status.ThrowIfFailed("Failed to create renderer device.");
        if (nativeHandle.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native device handle was null.");
        }
        return new RendererDevice(DeviceHandle.FromNative(nativeHandle.Handle));
    }

    internal NativeDeviceHandle DangerousGetHandle() => new() { Handle = _handle.DangerousGetHandle() };
    internal DeviceHandle Handle => _handle;

    public RendererCapabilities GetCapabilities()
    {
        ThrowIfDisposed();
        var status = NativeMethods.Device.GetCapabilities(DangerousGetHandle(), out var nativeCaps);
        status.ThrowIfFailed("Failed to query device capabilities.");
        return RendererCapabilities.FromNative(nativeCaps);
    }

    public RendererContext CreateContext(uint width, uint height)
    {
        ThrowIfDisposed();
        var status = NativeMethods.Context.Create(DangerousGetHandle(), width, height, out var nativeContext);
        status.ThrowIfFailed("Failed to create renderer context.");
        var handle = ContextHandle.FromNative(nativeContext.Handle);
        return new RendererContext(this, handle, width, height);
    }

    public RendererFence CreateFence()
    {
        ThrowIfDisposed();
        var status = NativeMethods.Fence.Create(DangerousGetHandle(), out var nativeFence);
        status.ThrowIfFailed("Failed to create renderer fence.");
        if (nativeFence.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native fence handle was null.");
        }
        var handle = FenceHandleSafe.FromNative(nativeFence.Handle);
        return new RendererFence(this, handle);
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

    public static AdapterInfo[] EnumerateAdapters()
    {
        unsafe
        {
            var status = NativeMethods.Device.EnumerateAdapters((AdapterDescription*)null, 0, out var count);
            status.ThrowIfFailed("Failed to enumerate adapters.");

            if (count == 0)
            {
                return Array.Empty<AdapterInfo>();
            }

            AdapterDescription[] managed = ArrayPool<AdapterDescription>.Shared.Rent((int)count);
            try
            {
                fixed (AdapterDescription* ptr = managed)
                {
                    status = NativeMethods.Device.EnumerateAdapters(ptr, count, out count);
                    status.ThrowIfFailed("Failed to enumerate adapters.");
                }

                var result = new AdapterInfo[(int)count];
                for (int i = 0; i < (int)count; i++)
                {
                    result[i] = AdapterInfo.FromNative(managed[i]);
                }
                return result;
            }
            finally
            {
                ArrayPool<AdapterDescription>.Shared.Return(managed, clearArray: true);
            }
        }
    }

    public static void RunSelfTest()
    {
        NativeMethods.RunSelfTest().ThrowIfFailed("Renderer self-test failed.");
    }
}
