using System;
using System.Runtime.ConstrainedExecution;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace RiveRenderer;

internal abstract class RefHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    protected RefHandle()
        : base(true)
    {
    }
}

internal sealed class DeviceHandle : RefHandle
{
    internal static DeviceHandle FromNative(nint handle)
    {
        var result = new DeviceHandle();
        result.SetHandle(handle);
        return result;
    }

    protected override bool ReleaseHandle()
    {
        var native = new NativeDeviceHandle { Handle = handle };
        var status = NativeMethods.Device.Release(native);
        return status == RendererStatus.Ok;
    }
}

internal sealed class ContextHandle : RefHandle
{
    internal static ContextHandle FromNative(nint handle)
    {
        var result = new ContextHandle();
        result.SetHandle(handle);
        return result;
    }

    protected override bool ReleaseHandle()
    {
        var native = new NativeContextHandle { Handle = handle };
        var status = NativeMethods.Context.Release(native);
        return status == RendererStatus.Ok;
    }
}

internal sealed class PathHandleSafe : RefHandle
{
    internal static PathHandleSafe FromNative(nint handle)
    {
        var result = new PathHandleSafe();
        result.SetHandle(handle);
        return result;
    }

    protected override bool ReleaseHandle()
    {
        var native = new NativePathHandle { Handle = handle };
        var status = NativeMethods.Path.Release(native);
        return status == RendererStatus.Ok;
    }
}

internal sealed class PaintHandleSafe : RefHandle
{
    internal static PaintHandleSafe FromNative(nint handle)
    {
        var result = new PaintHandleSafe();
        result.SetHandle(handle);
        return result;
    }

    protected override bool ReleaseHandle()
    {
        var native = new NativePaintHandle { Handle = handle };
        var status = NativeMethods.Paint.Release(native);
        return status == RendererStatus.Ok;
    }
}

internal sealed class RendererHandleSafe : RefHandle
{
    internal ContextHandle Context { get; }
    private readonly bool _addRef;

    private RendererHandleSafe(ContextHandle context)
    {
        Context = context;
        Context.DangerousAddRef(ref _addRef);
    }

    internal static RendererHandleSafe FromNative(nint handle, ContextHandle context)
    {
        var result = new RendererHandleSafe(context);
        result.SetHandle(handle);
        return result;
    }

    protected override bool ReleaseHandle()
    {
        var native = new NativeRendererHandle { Handle = handle };
        var status = NativeMethods.Renderer.Release(native);
        if (_addRef)
        {
            Context.DangerousRelease();
        }
        return status == RendererStatus.Ok;
    }
}

internal sealed class BufferHandleSafe : RefHandle
{
    internal static BufferHandleSafe FromNative(nint handle)
    {
        var result = new BufferHandleSafe();
        result.SetHandle(handle);
        return result;
    }

    protected override bool ReleaseHandle()
    {
        var native = new NativeBufferHandle { Handle = handle };
        var status = NativeMethods.Buffer.Release(native);
        return status == RendererStatus.Ok;
    }
}

internal sealed class ImageHandleSafe : RefHandle
{
    internal static ImageHandleSafe FromNative(nint handle)
    {
        var result = new ImageHandleSafe();
        result.SetHandle(handle);
        return result;
    }

    protected override bool ReleaseHandle()
    {
        var native = new NativeImageHandle { Handle = handle };
        var status = NativeMethods.Image.Release(native);
        return status == RendererStatus.Ok;
    }
}

internal sealed class FontHandleSafe : RefHandle
{
    internal static FontHandleSafe FromNative(nint handle)
    {
        var result = new FontHandleSafe();
        result.SetHandle(handle);
        return result;
    }

    protected override bool ReleaseHandle()
    {
        var native = new NativeFontHandle { Handle = handle };
        var status = NativeMethods.Font.Release(native);
        return status == RendererStatus.Ok;
    }
}

internal sealed class ShaderHandleSafe : RefHandle
{
    internal static ShaderHandleSafe FromNative(nint handle)
    {
        var result = new ShaderHandleSafe();
        result.SetHandle(handle);
        return result;
    }

    protected override bool ReleaseHandle()
    {
        var native = new NativeShaderHandle { Handle = handle };
        var status = NativeMethods.Shader.Release(native);
        return status == RendererStatus.Ok;
    }
}

internal sealed class FenceHandleSafe : RefHandle
{
    internal static FenceHandleSafe FromNative(nint handle)
    {
        var result = new FenceHandleSafe();
        result.SetHandle(handle);
        return result;
    }

    protected override bool ReleaseHandle()
    {
        var native = new NativeFenceHandle { Handle = handle };
        var status = NativeMethods.Fence.Release(native);
        return status == RendererStatus.Ok;
    }
}

internal sealed class SurfaceHandleSafe : RefHandle
{
    internal DeviceHandle Device { get; }
    internal ContextHandle Context { get; }
    private bool _deviceAddRef;
    private bool _contextAddRef;

    private SurfaceHandleSafe(DeviceHandle device, ContextHandle context)
    {
        Device = device;
        Context = context;
        Device.DangerousAddRef(ref _deviceAddRef);
        Context.DangerousAddRef(ref _contextAddRef);
    }

    internal static SurfaceHandleSafe FromNative(nint handle, DeviceHandle device, ContextHandle context)
    {
        var result = new SurfaceHandleSafe(device, context);
        result.SetHandle(handle);
        return result;
    }

    protected override bool ReleaseHandle()
    {
        var native = new NativeSurfaceHandle { Handle = handle };
        var status = NativeMethods.Surface.Release(native);
        if (_contextAddRef)
        {
            Context.DangerousRelease();
        }
        if (_deviceAddRef)
        {
            Device.DangerousRelease();
        }
        return status == RendererStatus.Ok;
    }
}
