param(
  [string]$BuildDir = "build",
  [string]$Configuration = "Debug",
  [string]$VcpkgRoot = $env:VCPKG_ROOT,
  [string]$Generator = "Visual Studio 17 2022",
  [string]$Architecture = "x64",
  [switch]$Clean,
  [switch]$NoTests
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$repoRootFull = [System.IO.Path]::GetFullPath($repoRoot)
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRootFull $BuildDir }
$buildPathFull = [System.IO.Path]::GetFullPath($buildPath)

if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
  $VcpkgRoot = "C:\dev\vcpkg"
}

$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (!(Test-Path $toolchain)) {
  throw "vcpkg toolchain not found at $toolchain. Run scripts/bootstrap-dev.ps1 first, or pass -VcpkgRoot."
}

if ($Clean -and (Test-Path $buildPathFull)) {
  $repoPrefix = $repoRootFull.TrimEnd('\') + '\'
  if (!$buildPathFull.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean build directory outside the repository: $buildPathFull"
  }

  Write-Host "Cleaning $buildPathFull"
  Remove-Item -LiteralPath $buildPathFull -Recurse -Force
}

Write-Host "Configuring $buildPathFull"
cmake -S $repoRootFull -B $buildPathFull -G $Generator -A $Architecture "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

Write-Host "Building $Configuration"
cmake --build $buildPathFull --config $Configuration
if ($LASTEXITCODE -ne 0) {
  Write-Warning "Build failed. If the error is LNK1168 on WavePreviewShellExtension.dll, Explorer or prevhost still has the shell extension loaded."
  Write-Warning "For the development shell extension cycle, run scripts\rebuild-register-dev.ps1 -IncludePreviewHandler so shell hosts are stopped before build."
  exit $LASTEXITCODE
}

if (!$NoTests) {
  Write-Host "Running tests"
  ctest --test-dir $buildPathFull -C $Configuration --output-on-failure
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

Write-Host "Full build completed successfully."
