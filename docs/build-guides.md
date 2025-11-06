# Build Guides

## Native Builds

| Platform | Script | Notes |
| --- | --- | --- |
| macOS (arm64/x64) | `scripts/build-macos.sh [debug|release]` | Requires Xcode + command line tools. Artifacts staged to `artifacts/native/osx-<arch>/<config>` and `dotnet/RiveRenderer/runtimes/osx-<arch>/native`. |
| Linux (x64) | `scripts/build-linux.sh [debug|release]` | Needs Ninja, CMake, and Vulkan SDK (when enabling Vulkan). Artifacts staged similar to macOS. |
| Windows (x64/ARM64) | `scripts/build-windows.ps1 -Configuration Release` | Run in a VS Developer PowerShell. Copies outputs into runtimes and artifact staging. |
| Browser (WASM) | `scripts/build-wasm.sh [debug|release]` | Requires an active Emscripten environment (`emcmake`/`emmake`). Outputs staged to `dotnet/RiveRenderer/runtimes/browser-wasm/native/<config>`. |

Driver scripts:

- `scripts/build.sh [-c Release] [--skip-managed]` orchestrates macOS/Linux native builds plus managed packaging.
- `scripts/build.ps1 -Configuration Release [-SkipManaged]` orchestrates Windows native build plus managed packaging.

## Managed Build & Packaging

```bash
scripts/build-managed.sh -c Release
```

Restores, builds, runs tests (including smoke tests), and packs NuGet packages into `artifacts/nuget`.

## Toolchain Validation

```bash
scripts/validate-toolchains.sh
```

Detects the active platform and confirms that required compilers/SDKs are installed (macOS + Linux). On Windows, run:

```powershell
scripts/validate-toolchains.ps1 [-VerboseOutput]
```

The validation scripts gate Visual Studio, CMake, Ninja, Vulkan SDK, and Metal availability before kicking off native builds.

## Samples

- `samples/Avalonia/RiveRenderer.AvaloniaSample` – Avalonia desktop app that exercises GPU surface creation (D3D12 on Windows) and reports status for other platforms. See `docs/samples/avalonia-gpu-rendering.md` for usage instructions.
- `samples/Validation/RiveRenderer.RenderValidation` – Console harness used by CI render validation job. See `docs/samples/render-validation.md`.

## Code Quality

- `scripts/check-clang-format.sh` – verifies C/C++ formatting for `renderer_ffi`.
- `scripts/run-clang-tidy.sh` – runs clang-tidy with the repository configuration.
- `scripts/dotnet-format-verify.sh` – checks .NET projects with `dotnet format --verify-no-changes`.
- CI job `code-quality` installs required tooling and executes these scripts alongside .NET analyzers.
- `scripts/validate-render.sh` – configures native library paths and runs the render validation console app.

## Continuous Integration

See `.github/workflows/build.yml` for the Release matrix. Debug builds and release automation are tracked in the roadmap (Phases 8–9).
