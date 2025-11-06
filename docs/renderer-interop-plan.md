# Renderer Interop Plan

Reference material:
- `/Users/wieslawsoltes/GitHub/SparseStrips/docs/ffi-interop-guidelines.md`
- `/Users/wieslawsoltes/GitHub/SparseStrips/` codebase (Rust/C# interoperability patterns)

## Phased Roadmap

1. [x] Phase 1 – Foundation & Requirements
   - [x] 1.1 Gather renderer architecture notes from `extern/river-renderer` and identify GPU backends (Vulkan/Metal/Direct3D/OpenGL) and feature surface that must be exposed.
     - Notes: `extern/river-renderer` mirrors the upstream `rive-cpp` runtime with GPU renderer under `renderer/`, covering Metal, Vulkan, D3D12, D3D11, OpenGL/WebGL, plus WebGPU via Dawn (`make_dawn.sh`) and MoltenVK (`make_moltenvk.sh`). Core capabilities include pixel-local-storage path rendering, ring-buffered GPU resource allocators (`rive/renderer/trivial_block_allocator.hpp`), gradient atlas management, tessellation pipelines (`gpu.hpp`), and backend-specific feature flags (clockwise/atomic interlock, advanced blend, clip planes) surfaced through `rive::gpu::PlatformFeatures`.
   - [x] 1.2 Catalogue existing interop, build, and testing assets in `SparseStrips` to reuse (build scripts, CI, FFI safety checks).
     - Notes: FFI guidance captured in `SparseStrips/docs/ffi-interop-guidelines.md`, Rust reference implementation in `SparseStrips/vello_cpu_ffi` (cbindgen config, panic catching, marshaling tests), and .NET bindings in `SparseStrips/dotnet/src/Vello.Native` (function-pointer caching via `delegate*`, `NativeLibraryLoader`, `FastPath` span helpers). Build/test automation includes `SparseStrips/build.sh`, `scripts/build-*.sh|ps1`, and .NET solution `SparseStrips/dotnet/Vello.sln` with unit/integration/benchmark projects for validating interop and performance.
   - [x] 1.3 Define performance targets (latency, throughput, frame pacing) and platform scope (Windows/macOS/Linux, x86_64/arm64).
     - Targets: Maintain ≤8.3 ms frame time (120 Hz) for complex scenes on discrete GPUs (Metal, Vulkan, D3D12); ≤12 ms on integrated GPUs (D3D11, Metal); ≤16 ms fallback on OpenGL/WebGPU. Command submission overhead per frame ≤0.2 ms CPU, resource uploads ≥12 GB/s sustained. Frame pacing jitter ≤0.5 ms over 120-frame windows.
     - Platform scope: Windows 11 (x64, ARM64 via D3D12/D3D11), macOS 13+ (x64/ARM64 via Metal & MoltenVK), Linux (x64 via Vulkan/OpenGL), and WebAssembly/WebGPU as stretch goal; ensure CI artifacts for each.
   - [x] 1.4 Draft initial threading, memory ownership, and handle lifecycle requirements for the renderer C-ABI.
     - Notes: Expose opaque handles (`rive_renderer_device_t`, `rive_renderer_context_t`, `rive_renderer_surface_t`, etc.) with explicit `*_create`/`*_retain`/`*_release` triads to support reference counting across threads. Command recording APIs remain thread-affine per context, while device/resource creation and queue submission are thread-safe via internal mutex/lock-free queues. All GPU buffers/textures allocated on native side; C# passes pinned spans with explicit size arguments, ownership transferred via `*_upload` functions returning fence tokens. Introduce per-thread error slots (mirroring `SparseStrips` TLS pattern) and require callers to flush/destroy in reverse order to avoid use-after-free during GC finalization.

2. [x] Phase 2 – C-ABI Surface Design
   - [x] 2.1 Enumerate renderer entry points (device/context creation, resource management, command submission, frame present) and map each to `extern "C"` symbols.
     - Planned API families:
       - Device/adapters: `rive_renderer_enumerate_adapters`, `rive_renderer_device_create`, `rive_renderer_device_retain`, `rive_renderer_device_release`, `rive_renderer_device_capabilities`.
       - Surfaces/swapchains: `rive_renderer_surface_create_from_window`, `rive_renderer_surface_create_offscreen`, `rive_renderer_surface_get_size`, `rive_renderer_surface_resize`, `rive_renderer_surface_present`, `rive_renderer_surface_release`.
       - Context/command encoding: `rive_renderer_context_create`, `rive_renderer_context_retain`, `rive_renderer_context_release`, `rive_renderer_context_begin_frame`, `rive_renderer_context_end_frame`, `rive_renderer_context_submit`.
       - Resources: buffer/texture creation (`*_upload`, `*_map`, `*_unmap`), shader/pipe caching (`rive_renderer_pipeline_create`), sampler management, gradient/paint state configuration.
       - Scene integration: path/shape submission (`rive_renderer_path_build`, `rive_renderer_draw_path`, `rive_renderer_flush`), text rasterization hooks, debug utilities (capture markers, GPU timing).
   - [x] 2.2 Specify enum and struct layouts following the FFI guideline rules (`#[repr(u8)]` for byte enums, explicit padding) and document expected sizes/alignments for each C# counterpart.
     - Key types (C++ ⇔ C# mirrors):
       - `rive_renderer_backend_t` (`enum class : uint8_t`): values mapping to Metal/Vulkan/D3D12/D3D11/OpenGL/WebGPU/Null; C# `enum RendererBackend : byte`.
       - `rive_renderer_adapter_desc_t` (`struct` with `#pragma pack(push, 1)`): fields include `backend` (u8), `vendor_id` (u16), `device_id` (u16), `name[256]`, GPU memory size `uint64_t`, queue flags `uint32_t`, explicit padding to keep 8-byte alignment; C# struct with `Pack = 1` and explicit padding bytes.
       - `rive_renderer_capabilities_t` (`struct`): booleans for platform features (raster ordering, atomic mode, advanced blend, clip planes, orientation), numeric limits (`uint64_t max_buffer_size`, `uint32_t max_texture_dimension`), `float max_sampler_anisotropy`, capability flags; verified via `static_assert`.
       - Resource handles as opaque pointer wrappers (`struct rive_renderer_device_t { void* handle; };`) to optimize marshaling; C# uses `nint`.
       - Error/status codes as `enum class rive_renderer_status_t : int32_t` providing consistent conversion for C# exceptions.
   - [x] 2.3 Define error model (status codes vs. result objects) including consistent prefix (e.g., `rive_renderer_status_*`) and conversion plan for C# exceptions.
     - Model: Functions return `rive_renderer_status_t` for fallible operations; constructors use out-parameters plus status code. On failure, thread-local slot stores UTF-8 message retrieved via `rive_renderer_get_last_error_message`. Warnings encoded via separate `rive_renderer_status_warning_*` range to avoid throwing in C# wrappers.
     - C# translation: `NativeStatusExtensions.ThrowOnError(status)` mapping to custom exception hierarchy (`RendererDeviceLostException`, `RendererOutOfMemoryException`, etc.), reusing TLS message retrieval pattern from SparseStrips (`NativeMethods.GetLastError()`).
   - [x] 2.4 Plan buffer/image ownership semantics (host/device pointers, lifetime rules) and synchronization primitives exposed through the ABI.
     - Ownership: All GPU allocations owned by renderer; ABI exposes upload methods that copy from caller-provided spans. Persistent mapped buffers represented via `rive_renderer_mapped_memory_t` with explicit `rive_renderer_buffer_unmap`. Fence/semaphore handles returned for CPU-GPU synchronization (`rive_renderer_fence_wait`). Capture semantics follow RAII with explicit release calls; C# wrappers implement `SafeHandle`.
     - Lifetime: Device owns queues; contexts reference device; surfaces reference both device and OS window handles; upload resources released after submission unless caller retains via `*_retain`. Threading: creation/destroy safe across threads; command encoding restricted to context-affined thread; submission supports multithreaded queueing with `rive_renderer_queue_submit_batch`.
     - Synchronization: Provide `rive_renderer_queue_signal_fence`, `rive_renderer_queue_wait_fence`, `rive_renderer_queue_flush`. CPU-visible staging memory requires explicit `rive_renderer_flush_host_cache`.

3. [x] Phase 3 – C++ ABI Scaffolding
   - [x] 3.1 Create a native shim target inside `extern/river-renderer/renderer` (e.g., `renderer_ffi/`) that builds alongside existing premake workflow and exports the agreed `extern "C"` surface.
     - Notes: Added standalone `renderer_ffi` CMake project in the repo root and ported the shim out of the submodule so it builds independently (`renderer_ffi/CMakeLists.txt:3`). Authored export header/source skeleton with visibility macro and extern "C" entrypoints (`renderer_ffi/include/rive_renderer_ffi.h:1`, `renderer_ffi/src/rive_renderer_ffi.cpp:1`).
   - [x] 3.2 Implement ABI-safe type definitions in shared headers with `static_assert` layout checks against engine structs (adapter/capability descriptors, handles, frame options).
     - Notes: Declared packed structs/enums mirroring the planned ABI and enforced layout via compile-time assertions for adapter/capability/frame descriptors (`renderer_ffi/include/rive_renderer_ffi.h:68`).
   - [x] 3.3 Bridge device/context lifetimes by wrapping existing C++ classes (`rive::gpu::RenderContext`, device factories) with retain/release counts and null-check guards.
     - Notes: Implemented reference-counted device/context wrappers with thread-local error plumbing and retain/release helpers (`renderer_ffi/src/rive_renderer_ffi.cpp:24`).
   - [x] 3.4 Provide adapter enumeration, capability queries, and stubbed submission routines that forward into the renderer, leaving TODO markers where backend integration will flesh out real GPU calls.
     - Notes: Added null-backend enumeration, capability reporting, and placeholder begin/end/submit routines that currently return `unimplemented` for future backend hookups (`renderer_ffi/src/rive_renderer_ffi.cpp:49`).

4. [x] Phase 4 – GPU Backend Integration
   - [x] 4.1 Align renderer GPU abstraction with C ABI handles (e.g., opaque `rive_renderer_device_t`) and ensure backend-specific resources remain encapsulated.
     - Notes: Windows builds now create real D3D12 devices/queues and tie them to the opaque handles with ref-counted lifetime management (`renderer_ffi/src/rive_renderer_ffi.cpp:375`). Context creation wires into `RenderContextD3D12Impl::MakeContext`, instantiating allocators/command lists so future frame submission can operate on native resources (`renderer_ffi/src/rive_renderer_ffi.cpp:446`).
   - [x] 4.2 Implement adapter selection & capability queries, surfacing required GPU metadata (feature level, queue family, swapchain limits) through ABI structs.
     - Notes: Added DXGI-backed enumeration with cached adapter records, UTF-8 naming, and feature detection feeding capability structs (`renderer_ffi/src/rive_renderer_ffi.cpp:144`). Null backend remains available as a final entry for headless usage.
   - [x] 4.3 Design zero-copy upload paths for geometry, textures, and uniform data using staging buffers and explicit synchronization compatible with C# pinning patterns.
     - Notes: Implemented D3D12 headless render-target management plus begin/end/submit plumbing that reuses engine command lists, executes copy/direct queues with fence synchronization, and exposes a self-test harness for regression coverage (`renderer_ffi/src/rive_renderer_ffi.cpp:1026`).
   - [x] 4.4 Add fallback/headless paths (CPU raster or null renderer) for environments lacking GPU access, ensuring consistent ABI behavior.
     - Notes: Added null-backend device/context handling that records frames into CPU memory while keeping the same retain/release semantics and integrated the path into the self-test routine for environments without GPU support (`renderer_ffi/src/rive_renderer_ffi.cpp:996`, `renderer_ffi/src/rive_renderer_ffi.cpp:1282`).
   - [x] 4.5 Surface renderer feature APIs through the C ABI (paths, paints, text, images, state stack).
     - Notes: Added handles and entry points for path construction, paint configuration, renderer state/draw control, text shaping, image decode, gradient shaders, image meshes, and GPU buffer upload (`renderer_ffi/include/rive_renderer_ffi.h`, `renderer_ffi/src/rive_renderer_ffi.cpp`). Path commands (move/line/cubic/close), fill-rule management, paint stroke/fill/blend configuration, renderer save/restore/transform/clip, text-to-path conversion, image drawing/meshes, gradient creation/assignment, and buffer upload entry points are now exposed. The shim now expects prebuilt river-renderer libraries (produced via `extern/river-renderer/build/build_rive.sh --with_rive_text`) and will fail the configure step with guidance if the artifacts are missing.

5. [x] Phase 5 – C# Binding Layer
   - [x] 5.1 Create a `dotnet` project mirroring `SparseStrips` interop scaffolding that generates `LibraryImport` bindings with source-friendly partial methods.
     - Notes: Added `dotnet/RiveRenderer` library targeting .NET 8 with generated P/Invokes split across partial native stubs (`dotnet/RiveRenderer/RiveRenderer.csproj`, `dotnet/RiveRenderer/Native`).
   - [x] 5.2 Define managed struct/enum mirrors with `[StructLayout(LayoutKind.Sequential, Pack = 1)]` and explicit padding fields aligned to C++ definitions.
     - Notes: Introduced enums and blittable structs for adapters, capabilities, matrices, and frame options mirroring the ABI layout (`dotnet/RiveRenderer/Enums.cs`, `dotnet/RiveRenderer/Structs.cs`).
   - [x] 5.3 Implement safe wrapper classes (`RendererDevice`, `RendererContext`, `RenderPath`, `RenderPaint`, `RenderText`, etc.) that manage native handles, GC finalizers, and `Dispose` patterns without introducing allocations in hot paths.
     - Notes: Implemented `SafeHandle`-backed wrappers and high-level device/context/path/paint/renderer classes (`dotnet/RiveRenderer/Handles.cs`, `RendererDevice.cs`, `RendererContext.cs`, `RenderPath.cs`, `RenderPaint.cs`, `Renderer.cs`, `RendererSurface.cs`, `RendererSurfaceOptions.cs`, `RendererMetalSurfaceOptions.cs`, `RendererFence.cs`).
   - [x] 5.4 Bind high-level drawing APIs: path construction (`RenderPath` commands, contour iteration), stroke/fill configuration (`RenderPaint` solid, gradient, image paints, blend modes), text layout/shaping (glyph runs, metrics), image uploads, state stack (save/restore, transforms, clips), and render submission to managed counterparts.
     - Notes: Managed wrappers now expose path commands, paint configuration, and renderer operations backed by the new C ABI endpoints (`renderer_ffi/include/rive_renderer_ffi.h:90`, `renderer_ffi/src/rive_renderer_ffi.cpp:1000`, `dotnet/RiveRenderer/RenderPath.cs`). Text/image bindings will build on subsequent native work.
   - [x] 5.5 Provide ergonomic managed helpers (builder patterns, disposable contexts) that minimize allocations while exposing the full vector feature set.
     - Notes: Added `FrameOptions.Create` helpers, context lifecycle methods, and managed wrappers designed for `using` patterns to minimize allocations while retaining native refs.
   - [x] 5.6 Integrate span-based APIs for buffers/textures and leverage `Unsafe.CopyBlock`/`MemoryMarshal` where necessary while complying with pinning rules from the guidelines.
     - Notes: Added span-friendly managed wrappers for render buffers, fonts, images, shaders, and text paths (`RenderBuffer`, `RenderImage`, `RenderFont`, `RenderShader`, `RendererContext.CreateTextPath/CreateLinearGradient/CreateRadialGradient`), with native `rive_renderer_buffer_upload`, image decode, gradient creation, and text path exports accepting raw spans. `LibraryImport` declarations now explicitly mark `CallConvCdecl` so Windows builds can opt back in without manual editing.

6. [ ] Phase 6 – Validation & Tooling
   - [x] 6.1 Port `SparseStrips` FFI conformance tests (native unit tests verifying sizes, C# `Marshal.SizeOf`, smoke tests) to cover all new types and entry points.
     - Notes: Added `dotnet/RiveRenderer.Tests` with struct layout checks for adapter/capabilities/device/text-style mirrors so regressions are caught without requiring native binaries at test time.
   - [ ] 6.2 Add benchmark harnesses (native microbench, .NET BenchmarkDotNet) targeting draw-call throughput, CPU-GPU transfer latency, and frame presentation cost.
   - [ ] 6.3 Add rendering behavior tests that exercise vector features end-to-end: path stroking/filling, gradient paints, image paints, blend modes, text shaping, clipping, and state stack transitions. Capture pixel diffs against golden images for CPU fallback and sanity render checks on GPU backends.
   - [ ] 6.4 Create interop regression suites that validate managed wrappers call the correct native APIs (mock transports, handle lifetime stress tests, exception propagation).
   - [ ] 6.5 Establish CI workflows for cross-platform builds (premake/cmake + dotnet), artifact packaging, and publishing of native binaries alongside managed assemblies.
     - Notes: Native build now links against the river-renderer GPU/static libraries and copies the outputs into `dotnet/RiveRenderer/runtimes/<rid>/native` via the refreshed `scripts/build-*.sh|ps1` helpers. Remaining work: wire these scripts into CI, add publish steps, and validate Windows/Linux packaging.
   - [ ] 6.6 Introduce automated checking (clang-tidy with FFI rules, clang-format, analyzers) and static analysis for unsafe blocks and threading primitives.

7. [x] Phase 7 – Documentation & Samples
   - [ ] 7.1 Produce developer documentation describing ABI usage, handle lifecycle, and performance tuning, referencing the FFI guidelines for troubleshooting layout issues.
   - [ ] 7.2 Create sample integrations (Avalonia-first) showcasing GPU rendering, hot-reload loops, and error handling best practices.
     - Notes: Documented a null-backend CPU capture walkthrough (`docs/samples/null-backend-cpu.md`) to exercise the new gradient, image-mesh, and framebuffer APIs; full UI integrations remain outstanding.
   - [ ] 7.3 Publish migration notes for existing RiveSharp consumers, highlighting new GPU requirements and compatibility considerations.
   - [ ] 7.4 Prepare release packaging notes (NuGet layout, native library distribution) and coordinate semantic versioning across native and managed components.

8. [ ] Phase 8 – GPU Surface & Buffer Interop
   - [x] 8.1 Define host windowing contracts per platform (Windows HWND/DXGI, macOS CAMetalLayer, Linux VkSurfaceKHR) and gather required lifecycle hooks from the embedding app.
     - Notes: Authored `docs/renderer-surface-contracts.md` covering HWND/DXGI, CAMetalLayer, and VkSurfaceKHR requirements (creation, resize, presentation, teardown) so embedding apps can supply stable handles and handle device-loss.
   - [ ] 8.2 Extend the C ABI with surface/swapchain creation, resize, present, and destruction, mirroring `fiddle_context_*` patterns for Metal/Vulkan/D3D12 and mapping to managed `RendererSurface`.
     - Notes: D3D12 `HWND` + CAMetalLayer interop is live end-to-end, including managed helpers and native build outputs landing in the sample runtimes (`scripts/build-*.sh`). `rive_renderer_device_create_vulkan` now accepts host-provided VkInstance/Device/Queue handles (with managed `RendererDevice.CreateVulkan` wrapper), enabling bring-your-own swapchain scenarios; `rive_renderer_surface_create_vulkan`/`RendererContext.CreateSurfaceVulkan` are exported but currently return `unimplemented` until swapchain management, resize/present, and frame lifecycle wiring are completed.
     - Execution plan to finish implementation:
       1. **Swapchain/Resource Plumbing (Native):**
          - Extend `SurfaceHandle` with Vulkan swapchain fields (VkSurfaceKHR, VkSwapchainKHR, image views, fences/semaphores, command buffers, queue family indices, present mode, frame counters, resize flags).
          - Use `rive_vkb::VulkanSwapchain` (or equivalent inline logic) to create/recreate the swapchain, cache `RenderTargetVulkanImpl` instances per image, and track `vkutil::ImageAccess` transitions.
          - Implement `rive_renderer_surface_create_vulkan`, `rive_renderer_surface_resize`, `rive_renderer_surface_present`, and `rive_renderer_context_submit` for the Vulkan backend (acquire image → begin frame → flush via `RenderContextVulkanImpl` → queue submit → present → handle `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` by recreating swapchain).
          - Load required Vulkan function pointers via `PFN_vkGetInstanceProcAddr` (vkCreateSwapchainKHR, vkAcquireNextImageKHR, vkQueuePresentKHR, etc.) and wire host provided queues (graphics/present).
       2. **Windowing Integrations:**
          - Add creation helpers for Xlib (`VkXlibSurfaceCreateInfoKHR`) and Wayland; defer to MoltenVK on macOS (ensure CAMetalLayer → VkSurfaceKHR path documented and validated).
          - Expose minimal C ABI shims for host applications that don’t already have a VkSurfaceKHR (optional follow-up once native path stabilises).
       3. **Frame Lifecycle & Error Handling:**
          - Update `rive_renderer_context_begin_frame/end_frame/submit` to honour the Vulkan branch (command buffer reset, frame fence wait, image acquisition, flush, queue submit with semaphores).
          - Provide device-loss recovery hooks (report `device_lost`, cleanup/recreate resources) aligning with D3D12/Metal semantics.
       4. **Managed Bindings & Samples:**
          - Finalise `RendererContext.CreateSurfaceVulkan` once native path is functional; add Linux/MoltenVK runtime checks and status messaging (parity with Windows sample).
          - Extend the Avalonia sample (or add an SDL/Skia minimal harness) to render via Vulkan on Linux, exercising resize/vsync/present.
       5. **Build, Packaging, and Docs:**
          - Update `scripts/build-linux.sh` (and macOS when MoltenVK is ready) to stage Vulkan-enabled binaries in runtime folders (ensure RID probing picks them up).
          - Provide troubleshooting guides (required Vulkan extensions, queue family requirements, environment variables) and refresh `docs/samples/avalonia-gpu-rendering.md` once Vulkan rendering is enabled.
   - [ ] 8.3 Expose GPU buffer map/unmap and synchronization primitives (fences/semaphores) across backends so persistent uploads bypass staging paths; update managed bindings with safe pinning helpers.
     - Notes: Introduced shared `rive_renderer_buffer_map`/`unmap` entry points with managed `RenderBuffer.Mapping` helpers and added D3D12 fence primitives + managed `RendererFence` to coordinate queue completion; additional backends still need plumbing and cross-queue semantics.
   - [ ] 8.4 Verify backend coverage (Metal/Vulkan/D3D12/D3D11/OpenGL/Null) with representative smoke tests and document unsupported combinations.

9. [ ] Phase 9 – Samples & CI Expansion
   - [x] 9.1 Build an Avalonia sample that drives the new surface APIs on each desktop platform, including resize/vsync error handling and device-loss recovery.
     - Notes: `samples/Avalonia/RiveRenderer.AvaloniaSample` now initialises the native renderer from Avalonia, creates a D3D12 swapchain on Windows, animates frames, and handles resize/device-loss via SafeHandle reinitialisation. macOS/Linux currently fall back to the null backend until Metal/Vulkan surfaces are fully wired; the status text reflects the active backend.
   - [ ] 9.2 Add automated render-validation tests (golden image diffs for CPU/null backend, checksum-based probes for GPU backends) runnable in CI with headless adapters.
   - [ ] 9.3 Provision CI runners with required GPU SDKs (metal/d3d/vulkan) and integrate native + managed build/test/publish steps, including artifact signing where needed.
   - [ ] 9.4 Expand docs with platform-specific setup guides and troubleshooting for the sample + CI flows.


### Build & Release Automation

- Notes: Each supported platform now requires dedicated build scripts, native artifact staging, managed packaging, and CI automation. See `docs/build-and-release-plan.md` for detailed tasks covering native builds, managed packing, CI matrix, release pipeline, and README updates.
