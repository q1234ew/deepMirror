param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [string]$SdkRoot = "",
    [switch]$LegacyUsb
)
$ErrorActionPreference = "Stop"
$env:PATH = (Join-Path $QtRoot "bin") + ";" + $env:PATH
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot "build-windows"
$cmakeArgs = @("-S", $projectRoot, "-B", $buildDir, "-G", "Visual Studio 17 2022", "-A", "x64", "-DCMAKE_PREFIX_PATH=$QtRoot")
if ($SdkRoot) { $cmakeArgs += "-DINSIGHT_SDK_ROOT=$SdkRoot" }
$cmakeArgs += "-DINSIGHT_SPLIT_USB=$(if ($LegacyUsb) {'OFF'} else {'ON'})"
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& cmake --build $buildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
& ctest --test-dir $buildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
& (Join-Path $QtRoot "bin/windeployqt.exe") --release (Join-Path $buildDir "Release/deepmirror.exe")
if ($LASTEXITCODE -ne 0) { throw "Qt deployment failed" }
Write-Host "Ready: $buildDir/Release/deepmirror.exe"
