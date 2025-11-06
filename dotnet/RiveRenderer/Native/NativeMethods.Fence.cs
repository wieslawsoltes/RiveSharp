using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace RiveRenderer;

internal static partial class NativeMethods
{
    internal static partial class Fence
    {
        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_fence_create")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Create(
            NativeDeviceHandle device,
            out NativeFenceHandle fence);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_fence_retain")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Retain(NativeFenceHandle fence);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_fence_release")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Release(NativeFenceHandle fence);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_fence_get_completed_value")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus GetCompletedValue(
            NativeFenceHandle fence,
            out ulong value);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_fence_wait")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Wait(
            NativeFenceHandle fence,
            ulong value,
            ulong timeoutMilliseconds);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_context_signal_fence")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Signal(
            NativeContextHandle context,
            NativeFenceHandle fence,
            ulong value);
    }
}
