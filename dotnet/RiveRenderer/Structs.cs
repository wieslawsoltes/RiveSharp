using System;
using System.Runtime.InteropServices;
using System.Text;

namespace RiveRenderer;

[StructLayout(LayoutKind.Sequential, Pack = 1)]
internal struct AdapterDescription
{
    public RendererBackend Backend;
    private byte _backendPadding;
    public ushort VendorId;
    public ushort DeviceId;
    public ushort SubsystemId;
    public ushort Revision;
    public ulong DedicatedVideoMemory;
    public ulong SharedSystemMemory;
    public uint Flags;
    private uint _reserved;
    private unsafe fixed byte _name[RendererConstants.MaxAdapterName];
    private unsafe fixed byte _reservedPadding[14];

    public string GetName()
    {
        unsafe
        {
            fixed (byte* ptr = _name)
            {
                int length = 0;
                while (length < RendererConstants.MaxAdapterName && ptr[length] != 0)
                {
                    length++;
                }
                return Encoding.UTF8.GetString(ptr, length);
            }
        }
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct FrameOptions
{
    public uint Width;
    public uint Height;
    public float DeltaTimeMilliseconds;
    public byte VSync;
    private byte _reserved0;
    private byte _reserved1;
    private byte _reserved2;

    public static FrameOptions Create(uint width, uint height, float deltaTimeMilliseconds = 0f, bool vsync = true)
    {
        return new FrameOptions
        {
            Width = width,
            Height = height,
            DeltaTimeMilliseconds = deltaTimeMilliseconds,
            VSync = (byte)(vsync ? 1 : 0),
        };
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct Mat2D
{
    public float XX;
    public float XY;
    public float YX;
    public float YY;
    public float TX;
    public float TY;

    public static Mat2D Identity => new()
    {
        XX = 1f,
        YY = 1f,
        XY = 0f,
        YX = 0f,
        TX = 0f,
        TY = 0f,
    };

    public static Mat2D FromArray(ReadOnlySpan<float> values)
    {
        if (values.Length < 6)
        {
            throw new ArgumentException("Matrix span must contain six elements.", nameof(values));
        }

        return new Mat2D
        {
            XX = values[0],
            XY = values[1],
            YX = values[2],
            YY = values[3],
            TX = values[4],
            TY = values[5],
        };
    }

    public readonly float[] ToArray() => new[] { XX, XY, YX, YY, TX, TY };
}

[StructLayout(LayoutKind.Sequential)]
public struct ImageSampler
{
    public ImageWrap WrapX;
    public ImageWrap WrapY;
    public ImageFilter Filter;
    private byte _reserved;

    public static ImageSampler LinearClamp => new()
    {
        WrapX = ImageWrap.Clamp,
        WrapY = ImageWrap.Clamp,
        Filter = ImageFilter.Bilinear,
    };
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
internal struct TextStyleOptions
{
    public float Size;
    public float LineHeight;
    public float LetterSpacing;
    public float Width;
    public float ParagraphSpacing;
    public TextAlign Align;
    public TextWrap Wrap;
    public TextDirection Direction;
    private byte _reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
internal struct NativeSurfaceCreateInfoD3D12Hwnd
{
    public nint Hwnd;
    public uint Width;
    public uint Height;
    public uint BufferCount;
    public RendererSurfaceFlags Flags;
    public uint PresentInterval;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
internal struct NativeSurfaceCreateInfoMetalLayer
{
    public nint Layer;
    public uint Width;
    public uint Height;
    public uint SampleCount;
    public RendererSurfaceFlags Flags;
}

internal static class RendererConstants
{
    public const int MaxAdapterName = 256;
}
