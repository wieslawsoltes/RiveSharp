using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;

namespace RiveRenderer;

internal static partial class NativeMethods
{
    internal static partial class Surface
    {
        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_surface_create_d3d12_hwnd")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus CreateD3D12Hwnd(
            NativeDeviceHandle device,
            NativeContextHandle context,
            in NativeSurfaceCreateInfoD3D12Hwnd info,
            out NativeSurfaceHandle surface);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_surface_create_metal_layer")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus CreateMetalLayer(
            NativeDeviceHandle device,
            NativeContextHandle context,
            in NativeSurfaceCreateInfoMetalLayer info,
            out NativeSurfaceHandle surface);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_surface_retain")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Retain(NativeSurfaceHandle surface);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_surface_release")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Release(NativeSurfaceHandle surface);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_surface_get_size")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus GetSize(
            NativeSurfaceHandle surface,
            out uint width,
            out uint height);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_surface_resize")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Resize(
            NativeSurfaceHandle surface,
            uint width,
            uint height);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_surface_present")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Present(
            NativeSurfaceHandle surface,
            uint presentInterval,
            RendererPresentFlags flags);
    }
}
