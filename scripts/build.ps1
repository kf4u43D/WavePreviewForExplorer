param(
  [string]$BuildDir = "build",
  [string]$Configuration = "Debug",
  [switch]$VerifyCliRenderOnFailure
)

$ErrorActionPreference = "Stop"

cmake --build $BuildDir --config $Configuration
if ($LASTEXITCODE -eq 0) {
  exit 0
}

$buildExitCode = $LASTEXITCODE
Write-Warning "CMake/MSBuild failed with exit code $buildExitCode."

if ($VerifyCliRenderOnFailure) {
  Write-Warning "Running CLI/render verification fallback. This does not replace a full CMake build."
  & (Join-Path $PSScriptRoot "verify-cli-render.ps1")
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
  exit $buildExitCode
}

Write-Host "For the current CLI/render workstream, run:" -ForegroundColor Yellow
Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -VerifyCliRenderOnFailure" -ForegroundColor Yellow
exit $buildExitCode
