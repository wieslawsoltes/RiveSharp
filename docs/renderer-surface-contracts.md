# GPU Surface Host Contracts

This document captures the responsibilities of an embedding application when wiring the native renderer to a platform window. The goal is to make the C ABI for Phase 8 explicit so each host can supply the correct handles, drive resize events, and respond to device-loss notifications.

## Common Requirements

- A surface ties together three objects: the renderer device, a swapchain that presents to the host window, and a `rive_renderer_context_t` that records GPU work. The host must keep all three alive for as long as the surface exists.
- Window resize events must call the surface resize entry point **before** the next `begin_frame`. This allows the renderer to recreate swapchain buffers without running into size mismatches.
- Presentation (`rive_renderer_surface_present`) should be driven from the UI thread that owns the native window handle to avoid deadlocks in platform compositors.
- Any failure status other than `ok` returned from a surface API should be treated as fatal for the current device/context pair. Destroy the surface, flush outstanding work, and rebuild the device/context to recover.

## Windows — `HWND` + DXGI (D3D12)

- Provide the renderer with a valid `HWND`. The handle must remain valid for the lifetime of the surface; destroying the window before releasing the surface leads to undefined behaviour.
- The host thread must pump the message loop regularly. The renderer waits on DXGI fences during present/resize and relies on the message loop to dispatch `WM_PAINT` and `WM_SIZE` messages.
- Resize handling:
  1. When the application receives `WM_SIZE`, update the cached client size.
  2. Call `rive_renderer_surface_resize(surface, width, height)` on the UI thread.
  3. The next `begin_frame` will pick up the new back-buffer.
- VSync is controlled through the surface descriptor. Pass `present_interval = 1` for vsync, `0` for immediate.
- Optional tearing (`RendererSurfaceFlags.AllowTearing`) is honoured when the platform reports support via `DXGI_FEATURE_PRESENT_ALLOW_TEARING`; the app must disable vsync to use it.
- Device-loss/restoration: `rive_renderer_surface_present` returns `renderer_status_t::device_lost` if DXGI reports `DXGI_ERROR_DEVICE_REMOVED`. Tear down the surface and device, then recreate them using the latest adapter selection.
- Passing `present_interval = 0` to `rive_renderer_surface_present` (or `RendererSurface.Present()`) uses the interval provided at surface creation, simplifying default vsync behaviour.

## macOS — `CAMetalLayer`

- Supply an Objective‑C `CAMetalLayer*` (exposed to native code as `void*`). The layer should be attached to the host view before creating the surface.
- Set `layer.pixelFormat = MTLPixelFormatBGRA8Unorm` and keep `presentsWithTransaction = NO` for lowest latency.
- The host must update `layer.drawableSize` on view resize and forward the new size to `rive_renderer_surface_resize`.
- Present must run on the main thread because Core Animation requires CAMetalLayer interaction on the UI run loop.
- If the layer is reparented or destroyed, release the surface first to avoid referencing freed Objective‑C objects.
- Use `RendererMetalSurfaceOptions` to configure MSAA sample count and vsync (default is enabled). Passing `RendererSurfaceFlags.AllowTearing` to `RendererSurface.Present` disables the post-present scheduler wait for immediate presentation.

## Linux — `VkSurfaceKHR`

- The host is expected to create the `VkSurfaceKHR` using the windowing toolkit it owns (GLFW, SDL, Wayland, XCB, …). Pass the created surface handle and the instance/device extensions used to construct it.
- Notify the renderer about window resizes (`rive_renderer_surface_resize`) immediately after the swapchain becomes suboptimal or the window geometry changes.
- On `VK_ERROR_DEVICE_LOST`, destroy the surface, device, and context before attempting to recreate them. The renderer will free swapchain images during `rive_renderer_surface_release`.
- Ensure the windowing toolkit keeps the surface valid until `rive_renderer_surface_release` returns; destroying the window beforehand invalidates the native handle.

## Lifetime Summary

| Event | Host Responsibility |
| --- | --- |
| Create surface | Ensure window (HWND/view/surface) exists and remains alive, then call `*_surface_create_*`. |
| Resize window | Call `rive_renderer_surface_resize` before the next frame and update any view-specific backing sizes (e.g., `CAMetalLayer.drawableSize`). |
| Render loop | Begin/end frames on the associated context, then call `rive_renderer_surface_present`. Handle non-`ok` status codes as device-loss and rebuild. |
| Tear-down | Call `rive_renderer_surface_release` before destroying the underlying window or layer. Release the context/device afterwards. |

These contracts give the native renderer enough information to create platform swapchains while keeping the embedding application in control of window ownership and threading.
