using System;
using RiveRenderer.Tests.TestUtilities;

namespace RiveRenderer.Tests;

public class RendererBehaviorTests
{
    [RequiresNativeLibraryFact]
    public void CanDrawGradientPath()
    {
        var backend = TryGetPreferredBackend();
        if (backend is null)
        {
            Console.WriteLine("Skipping CanDrawGradientPath: no GPU backend available.");
            return;
        }

        if (TryCreateDevice(backend.Value) is not RendererDevice device)
        {
            return;
        }

        using (device)
        {
            using var context = device.CreateContext(64, 64);
            using var path = context.CreatePath();
            using var paint = context.CreatePaint();

            path.MoveTo(0, 0);
            path.LineTo(32, 0);
            path.LineTo(32, 32);
            path.Close();

            var shader = context.CreateLinearGradient(
                startX: 0,
                startY: 0,
                endX: 32,
                endY: 32,
                colors: new uint[] { 0xFF0000FF, 0xFF00FF00 },
                stops: new float[] { 0f, 1f });

            paint.SetStyle(PaintStyle.Fill);
            paint.SetShader(shader);

            context.BeginFrame();
            using (var renderer = context.CreateRenderer())
            {
                renderer.DrawPath(path, paint);
            }
            context.EndFrame();
        }
    }

    private static RendererBackend? TryGetPreferredBackend()
    {
        try
        {
            foreach (var adapter in RendererDevice.EnumerateAdapters())
            {
                if (adapter.Backend != RendererBackend.Null)
                {
                    return adapter.Backend;
                }
            }
        }
        catch (DllNotFoundException)
        {
            return null;
        }
        catch (RendererException)
        {
            return null;
        }

        return null;
    }

    private static RendererDevice? TryCreateDevice(RendererBackend backend)
    {
        try
        {
            return RendererDevice.Create(backend);
        }
        catch (RendererException ex) when (ex.Status == RendererStatus.Unsupported)
        {
            Console.WriteLine($"Skipping CanDrawGradientPath: backend {backend} not supported ({ex.Message}).");
            return null;
        }
        catch (DllNotFoundException ex)
        {
            Console.WriteLine($"Skipping CanDrawGradientPath: native library unavailable ({ex.Message}).");
            return null;
        }
    }
}
