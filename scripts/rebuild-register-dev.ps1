[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string]$BuildDir = "build",
  [string]$Configuration = "Debug",
  [string]$VcpkgRoot = $env:VCPKG_ROOT,
  [string]$Generator = "Visual Studio 17 2022",
  [string]$Architecture = "x64",
  [string[]]$Extensions = @(".wav", ".wave"),
  [switch]$Clean,
  [switch]$NoTests,
  [switch]$IncludePreviewHandler,
  [switch]$NoStopShellHosts,
  [switch]$NoRestartExplorer
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$fullBuildScript = Join-Path $PSScriptRoot "full-build.ps1"
$registerScript = Join-Path $repoRoot "installer\scripts\register-dev.ps1"
$unregisterScript = Join-Path $repoRoot "installer\scripts\unregister-dev.ps1"

function Invoke-CheckedScript {
  param(
    [string]$Path,
    [hashtable]$Parameters,
    [string]$Description
  )

  if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Script not found: $Path"
  }

  Write-Host $Description
  $global:LASTEXITCODE = 0
  & $Path @Parameters
  if ($LASTEXITCODE -ne 0) {
    throw "$Description failed with exit code $LASTEXITCODE"
  }
}

function Stop-ShellHosts {
  $processNames = @("prevhost", "explorer")
  foreach ($processName in $processNames) {
    $processes = Get-Process -Name $processName -ErrorAction SilentlyContinue
    foreach ($process in $processes) {
      $target = "$($process.ProcessName) [$($process.Id)]"
      if ($PSCmdlet.ShouldProcess($target, "Stop process to unload AudioPreviewShellExtension.dll")) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
      }
    }
  }
}

$buildParams = @{
  BuildDir = $BuildDir
  Configuration = $Configuration
  Generator = $Generator
  Architecture = $Architecture
}
if (![string]::IsNullOrWhiteSpace($VcpkgRoot)) {
  $buildParams.VcpkgRoot = $VcpkgRoot
}
if ($Clean) {
  $buildParams.Clean = $true
}
if ($NoTests) {
  $buildParams.NoTests = $true
}

$unregisterParams = @{
  Extensions = $Extensions
}

$registerParams = @{
  BuildDir = $BuildDir
  Configuration = $Configuration
  Extensions = $Extensions
}
if ($IncludePreviewHandler) {
  $registerParams.IncludePreviewHandler = $true
}

Write-Warning "This development cycle can stop Explorer and prevhost so the shell extension DLL can be rebuilt."

if ($PSCmdlet.ShouldProcess("AudioPreview HKCU registry entries", "Unregister development shell providers")) {
  Invoke-CheckedScript -Path $unregisterScript -Parameters $unregisterParams -Description "Unregistering development shell providers"
}

if (!$NoStopShellHosts) {
  Stop-ShellHosts
}

if ($PSCmdlet.ShouldProcess($BuildDir, "Build AudioPreview")) {
  Invoke-CheckedScript -Path $fullBuildScript -Parameters $buildParams -Description "Building AudioPreview"
}

if ($PSCmdlet.ShouldProcess("AudioPreview HKCU registry entries", "Register development shell providers")) {
  Invoke-CheckedScript -Path $registerScript -Parameters $registerParams -Description "Registering development shell providers"
}

if (!$NoRestartExplorer) {
  if ($PSCmdlet.ShouldProcess("explorer.exe", "Start Explorer")) {
    Start-Process explorer.exe
  }
}

Write-Host "Development rebuild/register cycle completed."
