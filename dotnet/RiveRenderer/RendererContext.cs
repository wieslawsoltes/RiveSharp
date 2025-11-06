using System;
using System.Text;

namespace RiveRenderer;

public sealed class RendererContext : IDisposable
{
    private readonly RendererDevice _device;
    private readonly ContextHandle _handle;
    private bool _disposed;
    private uint _width;
    private uint _height;

    internal RendererContext(RendererDevice device, ContextHandle handle, uint width, uint height)
    {
        _device = device;
        _handle = handle;
        _width = width;
        _height = height;
    }

    public RendererDevice Device => _device;

    internal NativeContextHandle DangerousGetHandle() => new() { Handle = _handle.DangerousGetHandle() };

    internal void UpdateSizeFromSurface(uint width, uint height)
    {
        _width = width;
        _height = height;
    }

    public (uint Width, uint Height) Size
    {
        get
        {
            ThrowIfDisposed();
            var status = NativeMethods.Context.GetSize(DangerousGetHandle(), out var width, out var height);
            status.ThrowIfFailed("Failed to get context size.");
            _width = width;
            _height = height;
            return (width, height);
        }
    }

    public void Resize(uint width, uint height)
    {
        ThrowIfDisposed();
        var status = NativeMethods.Context.Resize(DangerousGetHandle(), width, height);
        status.ThrowIfFailed("Failed to resize context.");
        _width = width;
        _height = height;
    }

    public void BeginFrame(float deltaTimeMilliseconds = 0f, bool vsync = true)
    {
        BeginFrame(FrameOptions.Create(_width, _height, deltaTimeMilliseconds, vsync));
    }

    public void BeginFrame(FrameOptions options)
    {
        ThrowIfDisposed();
        if (options.Width == 0 || options.Height == 0)
        {
            options.Width = _width;
            options.Height = _height;
        }

        var status = NativeMethods.Context.BeginFrame(DangerousGetHandle(), in options);
        status.ThrowIfFailed("Failed to begin frame.");
    }

    public void EndFrame()
    {
        ThrowIfDisposed();
        var status = NativeMethods.Context.EndFrame(DangerousGetHandle());
        status.ThrowIfFailed("Failed to end frame.");
    }

    public void Submit()
    {
        ThrowIfDisposed();
        var status = NativeMethods.Context.Submit(DangerousGetHandle());
        status.ThrowIfFailed("Failed to submit frame.");
    }

    public void SignalFence(RendererFence fence, ulong value = 0)
    {
        ThrowIfDisposed();
        if (fence is null)
        {
            throw new ArgumentNullException(nameof(fence));
        }

        fence.ThrowIfDisposed();
        NativeMethods.Fence.Signal(DangerousGetHandle(), fence.DangerousGetHandle(), value)
            .ThrowIfFailed("Failed to signal fence.");
    }

    public RenderPath CreatePath(FillRule fillRule = FillRule.NonZero)
    {
        ThrowIfDisposed();
        var status = NativeMethods.Path.Create(DangerousGetHandle(), fillRule, out var native);
        status.ThrowIfFailed("Failed to create path.");
        if (native.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native path handle was null.");
        }
        var handle = PathHandleSafe.FromNative(native.Handle);
        return new RenderPath(handle, fillRule);
    }

    public RenderPaint CreatePaint()
    {
        ThrowIfDisposed();
        var status = NativeMethods.Paint.Create(DangerousGetHandle(), out var native);
        status.ThrowIfFailed("Failed to create paint.");
        if (native.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native paint handle was null.");
        }
        var handle = PaintHandleSafe.FromNative(native.Handle);
        return new RenderPaint(handle);
    }

    public Renderer CreateRenderer()
    {
        ThrowIfDisposed();
        var status = NativeMethods.Renderer.Create(DangerousGetHandle(), out var native);
        status.ThrowIfFailed("Failed to create renderer.");
        if (native.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native renderer handle was null.");
        }
        var handle = RendererHandleSafe.FromNative(native.Handle, _handle);
        return new Renderer(handle);
    }

    public RendererSurface CreateSurfaceWin32(nint hwnd, uint width, uint height, RendererSurfaceOptions options = default)
    {
        ThrowIfDisposed();
        if (hwnd == 0)
        {
            throw new ArgumentException("HWND must be non-zero.", nameof(hwnd));
        }

        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException("Win32 surface creation requires Windows.");
        }

        if (width == 0 || height == 0)
        {
            if (_width == 0 || _height == 0)
            {
                throw new InvalidOperationException("Context dimensions are unknown; provide explicit width and height.");
            }
            width = width == 0 ? _width : width;
            height = height == 0 ? _height : height;
        }

        var nativeInfo = options.ToNative(hwnd, width, height);
        var status = NativeMethods.Surface.CreateD3D12Hwnd(
            _device.DangerousGetHandle(),
            DangerousGetHandle(),
            in nativeInfo,
            out var nativeSurface);
        status.ThrowIfFailed("Failed to create D3D12 surface.");

        if (nativeSurface.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native surface handle was null.");
        }

        var handle = SurfaceHandleSafe.FromNative(nativeSurface.Handle, _device.Handle, _handle);
        UpdateSizeFromSurface(width, height);
        return new RendererSurface(_device, this, handle);
    }

    public RendererSurface CreateSurfaceMetalLayer(nint layer, uint width, uint height,
        RendererMetalSurfaceOptions options = default)
    {
        ThrowIfDisposed();
        if (layer == 0)
        {
            throw new ArgumentException("CAMetalLayer pointer must be non-zero.", nameof(layer));
        }

        if (!OperatingSystem.IsMacOS())
        {
            throw new PlatformNotSupportedException("Metal surface creation requires macOS.");
        }

        if (width == 0 || height == 0)
        {
            if (_width == 0 || _height == 0)
            {
                throw new InvalidOperationException("Context dimensions are unknown; provide explicit width and height.");
            }
            width = width == 0 ? _width : width;
            height = height == 0 ? _height : height;
        }

        var nativeInfo = options.ToNative(layer, width, height);
        var status = NativeMethods.Surface.CreateMetalLayer(
            _device.DangerousGetHandle(),
            DangerousGetHandle(),
            in nativeInfo,
            out var nativeSurface);
        status.ThrowIfFailed("Failed to create Metal surface.");

        if (nativeSurface.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native surface handle was null.");
        }

        var handle = SurfaceHandleSafe.FromNative(nativeSurface.Handle, _device.Handle, _handle);
        UpdateSizeFromSurface(width, height);
        return new RendererSurface(_device, this, handle);
    }

    public RendererSurface CreateSurfaceVulkan(
        nint surface,
        uint width,
        uint height,
        RendererVulkanSurfaceOptions options = default)
    {
        ThrowIfDisposed();
        if (surface == 0)
        {
            throw new ArgumentException("VkSurfaceKHR handle must be non-zero.", nameof(surface));
        }

        if (!OperatingSystem.IsLinux() && !OperatingSystem.IsMacOS())
        {
            throw new PlatformNotSupportedException("Vulkan surface creation requires Linux or macOS with MoltenVK.");
        }

        if (width == 0 || height == 0)
        {
            if (_width == 0 || _height == 0)
            {
                throw new InvalidOperationException("Context dimensions are unknown; provide explicit width and height.");
            }
            width = width == 0 ? _width : width;
            height = height == 0 ? _height : height;
        }

        var nativeInfo = options.ToNative(surface, width, height);
        var status = NativeMethods.Surface.CreateVulkan(
            _device.DangerousGetHandle(),
            DangerousGetHandle(),
            in nativeInfo,
            out var nativeSurface);
        status.ThrowIfFailed("Failed to create Vulkan surface.");

        if (nativeSurface.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native surface handle was null.");
        }

        var handle = SurfaceHandleSafe.FromNative(nativeSurface.Handle, _device.Handle, _handle);
        UpdateSizeFromSurface(width, height);
        return new RendererSurface(_device, this, handle);
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

    public RenderShader CreateLinearGradient(
        float startX,
        float startY,
        float endX,
        float endY,
        ReadOnlySpan<uint> colors,
        ReadOnlySpan<float> stops)
    {
        ThrowIfDisposed();
        if (colors.Length == 0 || colors.Length != stops.Length)
        {
            throw new ArgumentException("Gradient colors and stops must be non-empty and have matching lengths.");
        }

        NativeShaderHandle native;
        unsafe
        {
            fixed (uint* colorPtr = colors)
            fixed (float* stopPtr = stops)
            {
                NativeMethods.Shader.CreateLinearGradient(
                        DangerousGetHandle(),
                        startX,
                        startY,
                        endX,
                        endY,
                        colorPtr,
                        stopPtr,
                        (nuint)colors.Length,
                        out native)
                    .ThrowIfFailed("Failed to create linear gradient shader.");
            }
        }

        if (native.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native shader handle was null.");
        }

        return new RenderShader(ShaderHandleSafe.FromNative(native.Handle));
    }

    public RenderShader CreateRadialGradient(
        float centerX,
        float centerY,
        float radius,
        ReadOnlySpan<uint> colors,
        ReadOnlySpan<float> stops)
    {
        ThrowIfDisposed();
        if (radius <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(radius), "Radius must be positive.");
        }

        if (colors.Length == 0 || colors.Length != stops.Length)
        {
            throw new ArgumentException("Gradient colors and stops must be non-empty and have matching lengths.");
        }

        NativeShaderHandle native;
        unsafe
        {
            fixed (uint* colorPtr = colors)
            fixed (float* stopPtr = stops)
            {
                NativeMethods.Shader.CreateRadialGradient(
                        DangerousGetHandle(),
                        centerX,
                        centerY,
                        radius,
                        colorPtr,
                        stopPtr,
                        (nuint)colors.Length,
                        out native)
                    .ThrowIfFailed("Failed to create radial gradient shader.");
            }
        }

        if (native.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native shader handle was null.");
        }

        return new RenderShader(ShaderHandleSafe.FromNative(native.Handle));
    }

    public void CopyCpuFramebuffer(Span<byte> destination)
    {
        ThrowIfDisposed();
        var required = _width * _height * 4u;
        if ((uint)destination.Length < required)
        {
            throw new ArgumentException("Destination span is too small.", nameof(destination));
        }

        unsafe
        {
            fixed (byte* ptr = destination)
            {
                NativeMethods.Context.CopyCpuFramebuffer(DangerousGetHandle(), ptr, (nuint)required)
                    .ThrowIfFailed("Failed to copy CPU framebuffer. Only the null backend currently supports this call.");
            }
        }
    }

    public RenderBuffer CreateBuffer(BufferType type, nuint sizeInBytes, BufferFlags flags = BufferFlags.None, ReadOnlySpan<byte> initialData = default)
    {
        ThrowIfDisposed();
        var status = NativeMethods.Buffer.Create(DangerousGetHandle(), type, flags, sizeInBytes, out var native);
        status.ThrowIfFailed("Failed to create render buffer.");
        if (native.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native buffer handle was null.");
        }

        var handle = BufferHandleSafe.FromNative(native.Handle);
        var buffer = new RenderBuffer(handle, type, flags, sizeInBytes);
        if (!initialData.IsEmpty)
        {
            buffer.Upload(initialData);
        }
        return buffer;
    }

    public RenderImage DecodeImage(ReadOnlySpan<byte> data)
    {
        ThrowIfDisposed();
        if (data.IsEmpty)
        {
            throw new ArgumentException("Image data cannot be empty.", nameof(data));
        }

        NativeImageHandle native;
        unsafe
        {
            fixed (byte* ptr = data)
            {
                NativeMethods.Image.Decode(DangerousGetHandle(), ptr, (nuint)data.Length, out native)
                    .ThrowIfFailed("Failed to decode image.");
            }
        }

        if (native.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native image handle was null.");
        }

        return new RenderImage(ImageHandleSafe.FromNative(native.Handle));
    }

    public RenderFont DecodeFont(ReadOnlySpan<byte> data)
    {
        ThrowIfDisposed();
        if (data.IsEmpty)
        {
            throw new ArgumentException("Font data cannot be empty.", nameof(data));
        }

        NativeFontHandle native;
        unsafe
        {
            fixed (byte* ptr = data)
            {
                NativeMethods.Font.Decode(DangerousGetHandle(), ptr, (nuint)data.Length, out native)
                    .ThrowIfFailed("Failed to decode font.");
            }
        }

        if (native.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native font handle was null.");
        }

        return new RenderFont(FontHandleSafe.FromNative(native.Handle));
    }

    public RenderPath CreateTextPath(RenderFont font, string text, TextStyle style, FillRule fillRule = FillRule.NonZero)
    {
        ThrowIfDisposed();
        font.ThrowIfDisposed();
        if (text is null)
        {
            throw new ArgumentNullException(nameof(text));
        }

        var nativeStyle = style.ToNative();
        int byteCount = Encoding.UTF8.GetByteCount(text);
        NativePathHandle nativePath;
        unsafe
        {
            if (byteCount == 0)
            {
                nativePath = default;
                NativeMethods.Text.CreatePath(
                        DangerousGetHandle(),
                        font.DangerousGetHandle(),
                        null,
                        0,
                        in nativeStyle,
                        fillRule,
                        out nativePath)
                    .ThrowIfFailed("Failed to create text path.");
            }
            else
            {
                Span<byte> buffer = byteCount <= 512 ? stackalloc byte[byteCount] : new byte[byteCount];
                Encoding.UTF8.GetBytes(text, buffer);
                fixed (byte* ptr = buffer)
                {
                    nativePath = default;
                    NativeMethods.Text.CreatePath(
                            DangerousGetHandle(),
                            font.DangerousGetHandle(),
                            ptr,
                            (nuint)buffer.Length,
                            in nativeStyle,
                            fillRule,
                            out nativePath)
                        .ThrowIfFailed("Failed to create text path.");
                }
            }
        }

        if (nativePath.Handle == 0)
        {
            throw new RendererException(RendererStatus.InternalError, "Native text path handle was null.");
        }

        var pathHandle = PathHandleSafe.FromNative(nativePath.Handle);
        return new RenderPath(pathHandle, fillRule);
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(RendererContext));
        }
    }
}
