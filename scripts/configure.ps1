$ErrorActionPreference = "Stop"
$vcpkgRoot = $env:VCPKG_ROOT
if ([string]::IsNullOrWhiteSpace($vcpkgRoot)) { $vcpkgRoot = "C:\dev\vcpkg" }
$toolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (!(Test-Path $toolchain)) { throw "vcpkg toolchain not found at $toolchain. Run scripts/bootstrap-dev.ps1 first." }
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=$toolchain
