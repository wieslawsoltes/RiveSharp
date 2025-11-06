using System;
using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Platform;
using Avalonia.Threading;
using RiveRenderer;

namespace RiveRenderer.AvaloniaSample.Controls;

public sealed class RendererHostControl : Control
{
    public static readonly StyledProperty<string?> StatusMessageProperty =
        AvaloniaProperty.Register<RendererHostControl, string?>(nameof(StatusMessage));

    public string? StatusMessage
    {
        get => GetValue(StatusMessageProperty);
        set => SetValue(StatusMessageProperty, value);
    }

    private readonly Stopwatch _frameTimer = Stopwatch.StartNew();
    private readonly DateTime _startTime = DateTime.UtcNow;

    private DispatcherTimer? _timer;
    private RendererDevice? _device;
    private RendererContext? _context;
    private RendererSurface? _surface;
    private Renderer? _renderer;
    private RenderPath? _path;
    private RenderPaint? _paint;

    private RendererBackend _backend = RendererBackend.Null;
    private uint _width = 1;
    private uint _height = 1;
    private bool _pendingSurfaceResize;
    private bool _initialised;

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        InitializeRenderer();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        StopRendering();
        DisposeResources();
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        var arranged = base.ArrangeOverride(finalSize);
        UpdateSize(arranged);
        return arranged;
    }

    private void InitializeRenderer()
    {
        if (_initialised)
        {
            return;
        }

        _backend = SelectBackend();

        try
        {
            _device = RendererDevice.Create(_backend);

            var bounds = Bounds.Size;
            _width = (uint)Math.Max(1, Math.Round(bounds.Width));
            _height = (uint)Math.Max(1, Math.Round(bounds.Height));

            if (_width == 0 || _height == 0)
            {
                var topLevelSize = TopLevel.GetTopLevel(this)?.ClientSize ?? new Size(640, 360);
                _width = (uint)Math.Max(1, Math.Round(topLevelSize.Width));
                _height = (uint)Math.Max(1, Math.Round(topLevelSize.Height));
            }

            _context = _device.CreateContext(_width, _height);
            _renderer = _context.CreateRenderer();
            _paint = _context.CreatePaint();
            _paint.SetStyle(PaintStyle.Fill);
            _path = _context.CreatePath();

            StatusMessage = $"Renderer initialised using {_backend} backend ({_width}×{_height}).";
            _initialised = true;
        }
        catch (RendererException rex)
        {
            StatusMessage = $"Renderer initialisation failed: {rex.Status} ({rex.Message})";
            DisposeResources();
            return;
        }
        catch (Exception ex)
        {
            StatusMessage = $"Renderer initialisation failed: {ex.Message}";
            DisposeResources();
            return;
        }

        EnsureSurface();
        StartRendering();
    }

    private void DisposeResources()
    {
        _path?.Dispose();
        _path = null;

        _paint?.Dispose();
        _paint = null;

        _renderer?.Dispose();
        _renderer = null;

        _surface?.Dispose();
        _surface = null;

        _context?.Dispose();
        _context = null;

        _device?.Dispose();
        _device = null;

        _initialised = false;
    }

    private void StartRendering()
    {
        if (_timer != null)
        {
            return;
        }

        _frameTimer.Restart();
        _timer = new DispatcherTimer(TimeSpan.FromSeconds(1.0 / 60.0), DispatcherPriority.Render, (_, _) => RenderFrame());
        _timer.Start();
    }

    private void StopRendering()
    {
        if (_timer is not null)
        {
            _timer.Stop();
            _timer = null;
        }
    }

    private void RenderFrame()
    {
        if (_context == null)
        {
            return;
        }

        if (!EnsureSurface())
        {
            return;
        }

        float delta = (float)_frameTimer.Elapsed.TotalMilliseconds;
        _frameTimer.Restart();

        try
        {
            _context.BeginFrame(FrameOptions.Create(_width, _height, delta, vsync: true));
            DrawFrame();
            _context.EndFrame();
            _context.Submit();
            _surface?.Present(1, RendererPresentFlags.None);
        }
        catch (RendererException rex) when (rex.Status == RendererStatus.DeviceLost)
        {
            StatusMessage = "Device lost. Attempting to recover…";
            ResetRenderer();
        }
        catch (Exception ex)
        {
            StatusMessage = $"Rendering failed: {ex.Message}";
            StopRendering();
        }
    }

    private void DrawFrame()
    {
        if (_renderer == null || _path == null || _paint == null)
        {
            return;
        }

        float t = (float)(DateTime.UtcNow - _startTime).TotalSeconds;
        uint fill = ColorFromHue(t * 36f); // full cycle every ~10 seconds.

        _path.Rewind();
        _path.MoveTo(0, 0);
        _path.LineTo(_width, 0);
        _path.LineTo(_width, _height);
        _path.LineTo(0, _height);
        _path.Close();

        _paint.SetColor(fill);
        _renderer.DrawPath(_path, _paint);

        StatusMessage ??= string.Empty;
    }

    private uint ColorFromHue(float hueDegrees)
    {
        float h = hueDegrees % 360f;
        float s = 0.65f;
        float v = 0.85f;
        float c = v * s;
        float x = c * (1f - Math.Abs((h / 60f % 2f) - 1f));
        float m = v - c;

        float r, g, b;
        if (h < 60f) { r = c; g = x; b = 0f; }
        else if (h < 120f) { r = x; g = c; b = 0f; }
        else if (h < 180f) { r = 0f; g = c; b = x; }
        else if (h < 240f) { r = 0f; g = x; b = c; }
        else if (h < 300f) { r = x; g = 0f; b = c; }
        else { r = c; g = 0f; b = x; }

        byte R = (byte)((r + m) * 255f);
        byte G = (byte)((g + m) * 255f);
        byte B = (byte)((b + m) * 255f);
        byte A = 255;

        return (uint)(R | (G << 8) | (B << 16) | (A << 24));
    }

    private bool EnsureSurface()
    {
        if (_context == null || _device == null)
        {
            return false;
        }

        if (_backend == RendererBackend.D3D12)
        {
            var topLevel = TopLevel.GetTopLevel(this);
            var handle = topLevel?.TryGetPlatformHandle();
            if (handle == null || handle.Handle == IntPtr.Zero)
            {
                StatusMessage = "Waiting for window handle…";
                return false;
            }

            if (_surface == null)
            {
                var options = new RendererSurfaceOptions(bufferCount: 2, RendererSurfaceFlags.EnableVSync, presentInterval: 1);
                _surface = _context.CreateSurfaceWin32(handle.Handle, _width, _height, options);
                StatusMessage = $"Swapchain created ({_width}×{_height}).";
            }
            else if (_pendingSurfaceResize)
            {
                _surface.Resize(_width, _height);
                _pendingSurfaceResize = false;
            }

            return true;
        }

        if (_backend == RendererBackend.Metal)
        {
            StatusMessage = "Metal surface integration is not yet implemented. Falling back to null backend.";
            _backend = RendererBackend.Null;
            return false;
        }

        if (_surface != null)
        {
            _surface.Dispose();
            _surface = null;
        }

        return _backend != RendererBackend.Null;
    }

    private void UpdateSize(Size size)
    {
        uint width = (uint)Math.Max(1, Math.Round(size.Width));
        uint height = (uint)Math.Max(1, Math.Round(size.Height));
        if (width == 0 || height == 0)
        {
            return;
        }

        if (width == _width && height == _height)
        {
            return;
        }

        _width = width;
        _height = height;

        try
        {
            _context?.Resize(_width, _height);
            _pendingSurfaceResize = true;
        }
        catch (RendererException rex) when (rex.Status == RendererStatus.DeviceLost)
        {
            StatusMessage = "Resize caused device loss. Reinitialising…";
            ResetRenderer();
        }
        catch (Exception ex)
        {
            StatusMessage = $"Resize failed: {ex.Message}";
        }
    }

    private void ResetRenderer()
    {
        StopRendering();
        DisposeResources();
        InitializeRenderer();
    }

    private RendererBackend SelectBackend()
    {
        if (OperatingSystem.IsWindows())
        {
            return RendererBackend.D3D12;
        }

        if (OperatingSystem.IsMacOS())
        {
            return RendererBackend.Metal;
        }

        if (OperatingSystem.IsLinux())
        {
            StatusMessage = "Vulkan integration is pending; running with the null backend.";
            return RendererBackend.Null;
        }

        StatusMessage = "Unsupported platform; using null backend.";
        return RendererBackend.Null;
    }
}
