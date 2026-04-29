param(
  [string]$BuildDir = "build",
  [string]$VcpkgRoot = $env:VCPKG_ROOT,
  [string]$Generator = "Visual Studio 17 2022",
  [string]$Architecture = "x64"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) { $VcpkgRoot = "C:\dev\vcpkg" }
$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (!(Test-Path $toolchain)) { throw "vcpkg toolchain not found at $toolchain. Run scripts/bootstrap-dev.ps1 first." }

$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }

cmake -S $repoRoot -B $buildPath -G $Generator -A $Architecture "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
