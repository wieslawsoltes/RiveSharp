using System;

namespace RiveRenderer;

public sealed class Renderer : IDisposable
{
    private readonly RendererHandleSafe _handle;
    private bool _disposed;

    internal Renderer(RendererHandleSafe handle)
    {
        _handle = handle;
    }

    internal NativeRendererHandle DangerousGetHandle() => new() { Handle = _handle.DangerousGetHandle() };

    public void Save()
    {
        ThrowIfDisposed();
        NativeMethods.Renderer.Save(DangerousGetHandle()).ThrowIfFailed("Renderer save failed.");
    }

    public void Restore()
    {
        ThrowIfDisposed();
        NativeMethods.Renderer.Restore(DangerousGetHandle()).ThrowIfFailed("Renderer restore failed.");
    }

    public void Transform(in Mat2D transform)
    {
        ThrowIfDisposed();
        NativeMethods.Renderer.Transform(DangerousGetHandle(), in transform)
            .ThrowIfFailed("Renderer transform failed.");
    }

    public void DrawPath(RenderPath path, RenderPaint paint)
    {
        ThrowIfDisposed();
        path.ThrowIfDisposed();
        paint.ThrowIfDisposed();
        NativeMethods.Renderer.DrawPath(
                DangerousGetHandle(),
                path.DangerousGetHandle(),
                paint.DangerousGetHandle())
            .ThrowIfFailed("Renderer draw path failed.");
    }

    public void ClipPath(RenderPath path)
    {
        ThrowIfDisposed();
        path.ThrowIfDisposed();
        NativeMethods.Renderer.ClipPath(
                DangerousGetHandle(),
                path.DangerousGetHandle())
            .ThrowIfFailed("Renderer clip path failed.");
    }

    public void DrawImageMesh(
        RenderImage image,
        RenderBuffer vertices,
        RenderBuffer uvs,
        RenderBuffer indices,
        uint vertexCount,
        uint indexCount,
        BlendMode blendMode,
        float opacity = 1f,
        ImageSampler? sampler = null)
    {
        ThrowIfDisposed();
        image.ThrowIfDisposed();
        vertices.ThrowIfDisposed();
        uvs.ThrowIfDisposed();
        indices.ThrowIfDisposed();

        if (vertices.Type != BufferType.Vertex || uvs.Type != BufferType.Vertex)
        {
            throw new ArgumentException("Vertex and UV buffers must be of type Vertex.");
        }

        if (indices.Type != BufferType.Index)
        {
            throw new ArgumentException("Index buffer must be of type Index.");
        }

        if (vertexCount == 0 || indexCount == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(vertexCount), "Vertex and index counts must be non-zero.");
        }

        unsafe
        {
            ImageSampler samplerValue = sampler ?? ImageSampler.LinearClamp;
            ImageSampler* samplerPtr = sampler.HasValue ? &samplerValue : null;
            NativeMethods.Renderer.DrawImageMesh(
                    DangerousGetHandle(),
                    image.DangerousGetHandle(),
                    samplerPtr,
                    vertices.DangerousGetHandle(),
                    uvs.DangerousGetHandle(),
                    indices.DangerousGetHandle(),
                    vertexCount,
                    indexCount,
                    blendMode,
                    opacity)
                .ThrowIfFailed("Renderer draw image mesh failed.");
        }
    }

    public void DrawImage(RenderImage image, BlendMode blendMode, float opacity = 1f, ImageSampler? sampler = null)
    {
        ThrowIfDisposed();
        image.ThrowIfDisposed();
        unsafe
        {
            ImageSampler samplerValue = sampler ?? ImageSampler.LinearClamp;
            ImageSampler* samplerPtr = sampler.HasValue ? &samplerValue : null;
            NativeMethods.Renderer.DrawImage(
                    DangerousGetHandle(),
                    image.DangerousGetHandle(),
                    samplerPtr,
                    blendMode,
                    opacity)
                .ThrowIfFailed("Renderer draw image failed.");
        }
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

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(Renderer));
        }
    }
}
