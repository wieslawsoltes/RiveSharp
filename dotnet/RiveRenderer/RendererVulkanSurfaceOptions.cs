namespace RiveRenderer;

public readonly struct RendererVulkanSurfaceOptions
{
    public RendererVulkanSurfaceOptions(
        uint minImageCount = 2,
        uint presentMode = 0,
        RendererSurfaceFlags flags = RendererSurfaceFlags.EnableVSync)
    {
        MinImageCount = minImageCount == 0 ? 2u : minImageCount;
        PresentMode = presentMode;
        Flags = flags;
    }

    public uint MinImageCount { get; }
    public uint PresentMode { get; }
    public RendererSurfaceFlags Flags { get; }

    public static RendererVulkanSurfaceOptions Default => new RendererVulkanSurfaceOptions();

    internal NativeSurfaceCreateInfoVulkan ToNative(nint surface, uint width, uint height)
    {
        return new NativeSurfaceCreateInfoVulkan
        {
            Surface = surface,
            Width = width,
            Height = height,
            MinImageCount = MinImageCount,
            PresentMode = PresentMode,
            Flags = Flags,
        };
    }
}
