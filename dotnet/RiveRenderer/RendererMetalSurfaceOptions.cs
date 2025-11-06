namespace RiveRenderer;

public readonly struct RendererMetalSurfaceOptions
{
    public RendererMetalSurfaceOptions(uint sampleCount = 1, RendererSurfaceFlags flags = RendererSurfaceFlags.EnableVSync)
    {
        SampleCount = sampleCount == 0 ? 1u : sampleCount;
        Flags = flags;
    }

    public uint SampleCount { get; }
    public RendererSurfaceFlags Flags { get; }

    public static RendererMetalSurfaceOptions Default => new RendererMetalSurfaceOptions();

    internal NativeSurfaceCreateInfoMetalLayer ToNative(nint layer, uint width, uint height)
    {
        return new NativeSurfaceCreateInfoMetalLayer
        {
            Layer = layer,
            Width = width,
            Height = height,
            SampleCount = SampleCount,
            Flags = Flags,
        };
    }
}
