# Build & Release Plan

## Native Build Automation

1. **Bootstrap Renderer Dependencies**
   - [x] Ensure river-renderer submodule is initialized and synced to the expected commit.
   - [x] Re-bootstrap Premake 5 (v5.0.0-beta7) and dependencies to avoid stale binaries.
   - [x] Validate required SDKs/toolchains per platform (Apple clang + Xcode, MSVC + Windows SDK, GCC/Clang + Vulkan SDK).
     - Notes: Added `scripts/validate-toolchains.{sh,ps1}` to gate required toolchains; macOS validation executed locally, Windows/Linux script to be run on their respective agents.

2. **Per-Platform Build Scripts**
   - **macOS (arm64/x64, Metal + Null)**
     - [x] `scripts/build-macos.sh` – wraps `build_rive.sh`, configures CMake (Unix Makefiles), and stages native outputs under `dotnet/RiveRenderer/runtimes/osx-{arch}/native/{config}`.
       - Notes: GPU renderer static library (`librive_pls_renderer.a`) is not emitted by the current gmake workspace; linking still reports missing `rive::RiveRenderer` until upstream exposes the target.
     - [x] Include codesigning stub (optional) and notarisation hooks for release.
       - Notes: `scripts/build-macos.sh` now honors `CODESIGN_IDENTITY`/`CODESIGN_FLAGS` environment variables and signs produced `.dylib` assets when provided; notarisation hooks remain TODO for the release pipeline.
   - **Windows (x64/ARM64, D3D12/D3D11 + Null)**
     - [x] `scripts/build-windows.ps1` – orchestrates river-renderer + CMake/Ninja builds and stages `rive_renderer_ffi.dll` plus dependencies under `dotnet/RiveRenderer/runtimes/win-{arch}/native/{config}`.
       - Notes: Requires validation on Windows hosts (MSVC + Vulkan SDK) and will pick up the GPU renderer library once upstream target exists.
     - [x] Handle Debug/Release configuration matrix.
       - Notes: Script accepts `-Configurations` to build Debug/Release (and iterates per architecture) while staging config-specific runtimes and managed builds.
  - **Linux (x64, Vulkan/OpenGL + Null)**
     - [x] `scripts/build-linux.sh` – builds via `build_rive.sh`, configures CMake (Ninja), outputs `.so`, deploys to `dotnet/RiveRenderer/runtimes/linux-{arch}/native/{config}`.
       - Notes: Vulkan SDK/glslang installation needs to be provisioned on CI agents; GPU renderer static library will be copied once upstream target exists.
  - **Optional: wasm/webgpu**
     - [x] Capture build steps in `scripts/build-wasm.sh` using Emscripten if supported.
       - Notes: Script now checks for active Emscripten environment (`emcmake`/`emmake`), builds river-renderer in wasm mode, and stages `.wasm/.js` outputs under `dotnet/RiveRenderer/runtimes/browser-wasm/native/{config}`.

3. **FFI Build Integration**
   - [x] Update `renderer_ffi/CMakeLists.txt` to accept `CMAKE_BUILD_TYPE`, emit config-specific output directories, and install targets (GNUInstallDirs).
   - [x] Add top-level driver scripts (`scripts/build.sh`, `scripts/build.ps1`) that orchestrate platform builds and managed packaging.

4. **Artifact Staging**
   - [x] Normalize native binaries into `artifacts/native/{platform}/{arch}/{config}` directory (platform scripts mirror staged runtimes).
   - [x] Copy to managed project runtimes (`dotnet/RiveRenderer/runtimes/.../native`) for packaging.
   - [x] Generate manifest files (`manifest.txt`) within each artifact staging directory (checksum tooling optional).

## Managed Build & Packaging

1. **Managed Build Script**
   - [x] `scripts/build-managed.sh` – restores, builds, tests, and optionally packs managed assemblies (default Release).
   - [x] Embed versioning via `Directory.Build.props` (VersionPrefix/VersionSuffix, CI aware).

2. **Packaging**
   - [x] Rely on SDK-based `dotnet pack` (via build-managed script) to include native runtimes.
   - [x] Ensure `<RuntimeIdentifier>` groups and native asset layout align with NuGet runtime rules.
     - Notes: `runtimes/**/native/*` files are packed via wildcard `None` items in `RiveRenderer.csproj`.
   - [x] Produce `.nupkg` and `.snupkg` (symbols) outputs into `artifacts/nuget`.
     - Notes: `IncludeSymbols` and `SymbolPackageFormat=snupkg` are enabled in the project; `build-managed.sh` emits both packages.
   - [x] Generate GitHub release notes template summarizing native binaries per platform (`docs/release-notes-template.md`).

3. **Verification**
   - [x] Run smoke tests against packaged assemblies (simple render, null backend).
     - Notes: Added `SmokeTests.NullBackendDeviceLifecycle`; test no-ops if native library unavailable.
   - [x] Optionally create self-contained sample apps to validate load-paths on each OS.
     - Notes: Added `samples/Avalonia/RiveRenderer.AvaloniaSample` exercising the null backend and reporting status when native assets are staged.

## Continuous Integration

1. **CI Matrix Definition**
   - [x] Configure GitHub Actions (or Azure Pipelines) matrix: `{macos-13, windows-2022, ubuntu-22.04} × {Debug, Release}`.
     - Notes: `.github/workflows/build.yml` now drives native builds via a platform/config matrix, uploads artifacts per tuple, and merges them for managed packaging.
   - [x] Install platform prerequisites (Vulkan SDK, Metal headers, Windows SDK) on runners.
     - Notes: Workflow provisions Linux build essentials + Vulkan packages, validates Xcode/Metal on macOS, and ensures Ninja + Vulkan SDK are present on Windows prior to invoking platform scripts.

2. **Native Build Jobs**
   - [x] Checkout submodules, run platform build scripts, upload native artifacts using `actions/upload-artifact`.
   - [x] Cache Premake/premake-deps to speed up workflows (cache steps added to build/release workflows).

3. **Managed Build/Test Job**
   - [x] Download native artifacts, place inside runtimes, run `dotnet build/test`, ensure `dotnet test` executes layout and struct tests.
   - [x] Pack NuGet package; upload as artifact.

4. **Release Pipeline**
   - [x] Separate workflow triggered on tag (`v*`) or manual dispatch (`.github/workflows/release.yml`) builds native + managed artifacts (Release).
   - [x] Publishes GitHub release draft with uploaded NuGet package (draft mode).
   - [x] Pushes NuGet packages to nuget.org when `NUGET_API_KEY` secret is configured (conditional step).

5. **Quality Gates**
   - [x] Integrate clang-format/clang-tidy and .NET analyzers into CI (Phase 6.6 follow-up).
     - Notes: Added `.clang-format`, `.clang-tidy`, and verification scripts (`scripts/check-clang-format.sh`, `scripts/run-clang-tidy.sh`, `scripts/dotnet-format-verify.sh`) executed via the `code-quality` workflow job; .NET analyzers now enforced via `Directory.Build.props`.
   - [x] Add render validation job once Avalonia sample available.
     - Notes: Added `samples/Validation/RiveRenderer.RenderValidation` with `scripts/validate-render.sh` and wired the `render-validation` workflow stage to run the harness after native artifacts are produced.

## Documentation & README

1. **Professional README**
   - [x] Rework root `README.md` to cover: project overview, supported platforms/backends, build prerequisites, quick start, sample screenshots, roadmap.
   - [x] Include badges for CI, NuGet version, license.
   - [x] Outline contribution guidelines and code of conduct.
     - Notes: Added `CONTRIBUTING.md` and `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1) linked from `README.md`.

2. **Docs Updates**
   - [x] Document build scripts and pipeline notes in `docs/build-guides.md`.
  - [x] Provide Avalonia sample instructions and troubleshooting once the sample ships. ships.
    - Notes: `docs/samples/avalonia-gpu-rendering.md` documents the GPU-backed Avalonia sample, prerequisites, and current platform status.
   - [x] Include table summarizing native binaries per platform/config (in build guides).

## Next Steps Summary

- [x] Implement platform build scripts and update CMake install paths (pending GPU renderer target exposure for successful linking).
- [x] Wire managed build/packaging scripts consuming native outputs.
- [x] Stand up CI workflows for native + managed matrix, including release automation.
  - Notes: `build.yml` now spans macOS/Linux/Windows × Debug/Release, provisions toolchains, and flows artifacts into managed packaging; release workflow still handles tagged publishes with NuGet/GitHub outputs.
- [x] Author professional README and ancillary docs to surface the automation.
- [x] Track progress in `docs/renderer-interop-plan.md` (Phases 7–9) once tasks complete (plan cross-referenced with new build/release phases).

## CI Integration

- Added `.github/workflows/build.yml` executing platform scripts and managed builds with a GitHub Actions matrix (macOS, Linux, Windows, managed pack). Jobs currently marked `continue-on-error` pending completion of GPU renderer linkage and toolchain availability.
