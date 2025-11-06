namespace RiveRenderer;

public readonly struct RendererSurfaceOptions
{
    public RendererSurfaceOptions(
        uint bufferCount = 2,
        RendererSurfaceFlags flags = RendererSurfaceFlags.EnableVSync,
        uint presentInterval = 1)
    {
        BufferCount = bufferCount;
        Flags = flags;
        PresentInterval = presentInterval;
    }

    public uint BufferCount { get; }
    public RendererSurfaceFlags Flags { get; }
    public uint PresentInterval { get; }

    public static RendererSurfaceOptions Default => new RendererSurfaceOptions();

    internal NativeSurfaceCreateInfoD3D12Hwnd ToNative(nint hwnd, uint width, uint height)
    {
        uint bufferCount = BufferCount == 0 ? 2u : BufferCount;
        uint interval = PresentInterval == 0 ? 1u : PresentInterval;
        return new NativeSurfaceCreateInfoD3D12Hwnd
        {
            Hwnd = hwnd,
            Width = width,
            Height = height,
            BufferCount = bufferCount,
            Flags = Flags,
            PresentInterval = interval,
        };
    }
}
