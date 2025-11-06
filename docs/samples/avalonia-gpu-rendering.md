# Avalonia GPU Rendering Sample

The `samples/Avalonia/RiveRenderer.AvaloniaSample` project exercises the native renderer surfaces inside an Avalonia 11 desktop application. On Windows it creates a D3D12 swapchain bound to the window `HWND`, drives the frame lifecycle (`begin_frame → flush → present`), and animates a simple fill so you can verify that the GPU interop is working end-to-end. macOS and Linux currently fall back to the null backend while the Metal/Vulkan paths are finished.

## Prerequisites

- Stage the native binaries for your platform:
  - **Windows:** `pwsh ./scripts/build-windows.ps1 -Configurations Debug`
  - **macOS:** `./scripts/build-macos.sh debug`
  - **Linux:** `./scripts/build-linux.sh debug`
- Ensure the `dotnet/RiveRenderer/runtimes/<rid>/native/<config>` directories contain the freshly built `rive_renderer_ffi` library and supporting archives.
- Install the .NET 9.0 SDK (preview) that matches the repository’s global.json.

## Running the sample

```bash
dotnet run --project samples/Avalonia/RiveRenderer.AvaloniaSample/RiveRenderer.AvaloniaSample.csproj -c Debug
```

The window reports the backend it selected (D3D12 on Windows). You should see an animated colour fill rendered by the native renderer; the status message updates when the swapchain is resized, and device-loss triggers a full reset.

If the native binaries are missing or cannot be loaded the control reports guidance in the status text. Linux/macOS currently fall back to the null backend until the Vulkan/Metal surface plumbing is completed, so you will only see the animated GPU output on Windows.

## Next steps

- Extend the sample once Metal and Vulkan surfaces are implemented so the same animated view renders across macOS and Linux.
- Integrate a basic Rive scene once GPU swapchains are stable, replacing the placeholder solid fill.
