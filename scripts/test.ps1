param(
  [string]$BuildDir = "build",
  [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }

ctest --test-dir $buildPath -C $Configuration --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
