using System;

namespace RiveRenderer;

public enum RendererStatus : int
{
    Ok = 0,
    NullPointer = -1,
    InvalidHandle = -2,
    InvalidParameter = -3,
    OutOfMemory = -4,
    Unsupported = -5,
    DeviceLost = -6,
    Unimplemented = -7,
    InternalError = -8,
}

public enum RendererBackend : byte
{
    Unknown = 0,
    Null = 1,
    Metal = 2,
    Vulkan = 3,
    D3D12 = 4,
    D3D11 = 5,
    OpenGL = 6,
    WebGPU = 7,
}

[Flags]
public enum RendererDeviceFlags : uint
{
    None = 0,
    EnableValidation = 1 << 0,
    EnableDebugMarkers = 1 << 1,
    EnableDiagnostics = 1 << 2,
    Headless = 1 << 3,
}

[Flags]
public enum RendererFeatureFlags : uint
{
    None = 0,
    RasterOrdering = 1 << 0,
    AtomicPathRendering = 1 << 1,
    ClockwiseFill = 1 << 2,
    AdvancedBlend = 1 << 3,
    AdvancedBlendCoherent = 1 << 4,
    ClipPlanes = 1 << 5,
    BottomUpFramebuffer = 1 << 6,
    HeadlessSupported = 1 << 7,
}

public enum FillRule : byte
{
    NonZero = 0,
    EvenOdd = 1,
    Clockwise = 2,
}

public enum PaintStyle : byte
{
    Fill = 0,
    Stroke = 1,
}

public enum BufferType : byte
{
    Index = 0,
    Vertex = 1,
}

[Flags]
public enum BufferFlags : uint
{
    None = 0,
    MappedOnceAtInitialization = 1 << 0,
}

[Flags]
public enum BufferMapFlags : uint
{
    None = 0,
    InvalidateRange = 1 << 0,
    DiscardRange = 1 << 1,
}

public enum StrokeCap : byte
{
    Butt = 0,
    Round = 1,
    Square = 2,
}

public enum StrokeJoin : byte
{
    Miter = 0,
    Round = 1,
    Bevel = 2,
}

public enum BlendMode : byte
{
    SrcOver = 3,
    Screen = 14,
    Overlay = 15,
    Darken = 16,
    Lighten = 17,
    ColorDodge = 18,
    ColorBurn = 19,
    HardLight = 20,
    SoftLight = 21,
    Difference = 22,
    Exclusion = 23,
    Multiply = 24,
    Hue = 25,
    Saturation = 26,
    Color = 27,
    Luminosity = 28,
}

public enum ImageFilter : byte
{
    Bilinear = 0,
    Nearest = 1,
}

public enum ImageWrap : byte
{
    Clamp = 0,
    Repeat = 1,
    Mirror = 2,
}

public enum TextAlign : byte
{
    Left = 0,
    Right = 1,
    Center = 2,
}

public enum TextWrap : byte
{
    Wrap = 0,
    NoWrap = 1,
}

public enum TextDirection : byte
{
    Automatic = 0,
    Ltr = 1,
    Rtl = 2,
}

[Flags]
public enum RendererSurfaceFlags : uint
{
    None = 0,
    EnableVSync = 1 << 0,
    AllowTearing = 1 << 1,
}

[Flags]
public enum RendererPresentFlags : uint
{
    None = 0,
    AllowTearing = 1 << 0,
}

internal static class RendererStatusExtensions
{
    public static void ThrowIfFailed(this RendererStatus status, string? message = null)
    {
        if (status != RendererStatus.Ok)
        {
            string detail = message ?? TryGetNativeError(status) ?? $"Renderer operation failed with status {status}.";
            throw new RendererException(status, detail);
        }
    }

    private static string? TryGetNativeError(RendererStatus status)
    {
        try
        {
            unsafe
            {
                nuint length = NativeMethods.GetLastErrorMessage(null, 0);
                if (length == 0)
                {
                    return null;
                }

                var buffer = new byte[(int)length + 1];
                fixed (byte* ptr = buffer)
                {
                    NativeMethods.GetLastErrorMessage(ptr, (nuint)buffer.Length);
                }
                return System.Text.Encoding.UTF8.GetString(buffer, 0, (int)length);
            }
        }
        catch
        {
            return null;
        }
    }
}
