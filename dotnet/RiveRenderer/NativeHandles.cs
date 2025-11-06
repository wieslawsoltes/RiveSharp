using System.Runtime.InteropServices;

namespace RiveRenderer;

[StructLayout(LayoutKind.Sequential)]
internal struct NativeDeviceHandle
{
    public nint Handle;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeContextHandle
{
    public nint Handle;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativePathHandle
{
    public nint Handle;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativePaintHandle
{
    public nint Handle;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeRendererHandle
{
    public nint Handle;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeBufferHandle
{
    public nint Handle;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeImageHandle
{
    public nint Handle;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeFontHandle
{
    public nint Handle;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeShaderHandle
{
    public nint Handle;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeFenceHandle
{
    public nint Handle;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSurfaceHandle
{
    public nint Handle;
}
