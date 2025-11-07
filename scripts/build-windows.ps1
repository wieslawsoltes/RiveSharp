Param(
    [ValidateSet('Debug','Release')]
    [string[]]$Configurations = @('Release'),

    [ValidateSet('x64','ARM64')]
    [string[]]$Architectures = @('x64'),

    [switch]$SkipNative,
    [switch]$SkipManaged
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Resolve-Path (Join-Path $ScriptDir '..')

$BashCommand = Get-Command bash -ErrorAction SilentlyContinue
if (-not $BashCommand) {
    throw "Required command 'bash' not found. Ensure Git for Windows (with bash) is installed and on PATH."
}

$MakeCommand = Get-Command make -ErrorAction SilentlyContinue
if (-not $MakeCommand) {
    throw "Required command 'make' not found. Install GNU make (e.g. 'choco install make')."
}

function Invoke-BashScript {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Script,
        [string[]]$Arguments = @(),
        [switch]$Quiet
    )

    if ($Quiet) {
        & $BashCommand $Script @Arguments | Out-Null
    }
    else {
        & $BashCommand $Script @Arguments
    }
}

$premakeDir = Join-Path $RootDir 'extern/river-renderer/build/dependencies/premake-core/bin/release'
if (Test-Path (Join-Path $premakeDir 'premake5.exe')) {
    $env:PATH = "$premakeDir;$env:PATH"
}

$nativeLibPatterns = @(
    'rive_pls_renderer.lib',
    'rive.lib',
    'rive_decoders.lib',
    'rive_harfbuzz.lib',
    'rive_sheenbidi.lib',
    'rive_yoga.lib',
    'miniaudio.lib',
    'libpng.lib',
    'libjpeg.lib',
    'libwebp.lib',
    'zlib.lib'
)
$nativeSearchRoots = @(
    'extern/river-renderer/out',
    'extern/river-renderer/renderer/out',
    'extern/river-renderer/decoders/out'
)

function Find-NativeLib {
    param(
        [string]$Pattern,
        [string]$Config,
        [string]$Arch
    )

    $configLower = $Config.ToLower()
    $archLower = $Arch.ToLower()

    foreach ($root in $nativeSearchRoots) {
        $fullRoot = Join-Path $RootDir $root
        if (-not (Test-Path $fullRoot)) {
            continue
        }

        $matches = Get-ChildItem -Path $fullRoot -Recurse -Filter $Pattern -File -ErrorAction SilentlyContinue | Sort-Object FullName
        foreach ($match in $matches) {
            if ($match.FullName -like "*${archLower}_${configLower}*") {
                return $match.FullName
            }
        }
        if ($matches.Count -gt 0) {
            return $matches[0].FullName
        }
    }

    if ($Pattern -notlike 'liblib*' -and $Pattern.StartsWith('lib')) {
        $fallback = 'lib' + $Pattern
        return Find-NativeLib -Pattern $fallback -Config $Config -Arch $Arch
    }

    return $null
}

function Invoke-RiverRendererBuild {
    param(
        [string]$Arch,
        [string]$Config
    )

    Push-Location (Join-Path $RootDir 'extern/river-renderer')
    try {
        if (-not (Test-Path 'premake5.lua')) {
            New-Item -ItemType SymbolicLink -Path 'premake5.lua' -Target 'premake5_v2.lua' -Force | Out-Null
        }
        $env:RIVE_PREMAKE_ARGS = '--with_rive_text --with_rive_layout'
        Invoke-BashScript ./build/build_rive.sh -Arguments @('clean') -Quiet
        if (Test-Path 'out') {
            Remove-Item -Recurse -Force 'out'
        }
        Invoke-BashScript ./build/build_rive.sh -Arguments @($Config)
    }
    finally {
        Remove-Item Env:RIVE_PREMAKE_ARGS -ErrorAction SilentlyContinue
        Pop-Location
    }

    Push-Location (Join-Path $RootDir 'extern/river-renderer/renderer')
    try {
        $env:RIVE_PREMAKE_ARGS = '--with_rive_text --with_rive_layout'
        Invoke-BashScript ../build/build_rive.sh -Arguments @('clean') -Quiet
        if (Test-Path 'out') {
            Remove-Item -Recurse -Force 'out'
        }
        Invoke-BashScript ../build/build_rive.sh -Arguments @($Config)
    }
    finally {
        Remove-Item Env:RIVE_PREMAKE_ARGS -ErrorAction SilentlyContinue
        Pop-Location
    }

    $buildDir = Join-Path $RootDir "renderer_ffi/build-$Arch-$Config"
    $generator = 'Ninja'
    & cmake -S (Join-Path $RootDir 'renderer_ffi') -B $buildDir -G $generator -DCMAKE_BUILD_TYPE=$Config -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$($Config -eq 'Debug' ? 'Debug' : '')" | Write-Output
    & cmake --build $buildDir --config $Config | Write-Output

    $outputDir = Join-Path $buildDir "out/$Config"
    $ffiBinary = Join-Path $outputDir 'rive_renderer_ffi.dll'
    if (-not (Test-Path $ffiBinary)) {
        throw "Native build did not produce $ffiBinary"
    }

    $runtimeRoot = Join-Path $RootDir "dotnet/RiveRenderer/runtimes/win-$Arch/native/$Config"
    $artifactRoot = Join-Path $RootDir "artifacts/native/win-$Arch/$Config"
    New-Item -ItemType Directory -Force -Path $runtimeRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $artifactRoot | Out-Null
    Copy-Item $ffiBinary $runtimeRoot -Force

    foreach ($pattern in $nativeLibPatterns) {
        $source = Find-NativeLib -Pattern $pattern -Config $Config -Arch $Arch
        if ($null -ne $source) {
            Copy-Item $source $runtimeRoot -Force
        }
        else {
            Write-Warning "Missing $pattern for $Arch/$Config"
        }
    }

    if ($Config -eq 'Release') {
        $sharedRuntimeDir = Join-Path $RootDir "dotnet/RiveRenderer/runtimes/win-$Arch/native"
        New-Item -ItemType Directory -Force -Path $sharedRuntimeDir | Out-Null
        Get-ChildItem -Path $runtimeRoot -File | ForEach-Object {
            Copy-Item $_.FullName $sharedRuntimeDir -Force
        }
    }

    Copy-Item (Join-Path $runtimeRoot '*') $artifactRoot -Force
    (Get-ChildItem -Path $artifactRoot -File | Sort-Object Name | Select-Object -ExpandProperty Name) | Out-File (Join-Path $artifactRoot 'manifest.txt') -Encoding utf8
}

if (-not $SkipNative) {
    foreach ($config in $Configurations) {
        foreach ($arch in $Architectures) {
            Invoke-RiverRendererBuild -Arch $arch -Config $config
        }
    }
}

if (-not $SkipManaged) {
    foreach ($config in $Configurations) {
        Push-Location (Join-Path $RootDir 'dotnet')
        try {
            dotnet build RiveRenderer.sln -c $config | Write-Output
        }
        finally {
            Pop-Location
        }
    }
}
