using System;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;

namespace RiveRenderer;

internal static partial class NativeMethods
{
    internal static partial class Buffer
    {
        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_buffer_create")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Create(
            NativeContextHandle context,
            BufferType type,
            BufferFlags flags,
            nuint sizeInBytes,
            out NativeBufferHandle buffer);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_buffer_retain")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Retain(NativeBufferHandle buffer);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_buffer_release")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Release(NativeBufferHandle buffer);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_buffer_upload")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static unsafe partial RendererStatus Upload(
            NativeBufferHandle buffer,
            void* data,
            nuint length,
            nuint offset);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_buffer_map")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Map(
            NativeBufferHandle buffer,
            BufferMapFlags flags,
            out NativeMappedMemory mapping);

        [LibraryImport(LibraryName, EntryPoint = "rive_renderer_buffer_unmap")]
        [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
        internal static partial RendererStatus Unmap(
            NativeBufferHandle buffer,
            in NativeMappedMemory mapping,
            nuint writtenBytes);
    }
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeMappedMemory
{
    public nint Data;
    public nuint Length;
}
