param(
  [string]$BuildDir = "build",
  [string]$Configuration = "Release",
  [string]$OutputDir,
  [switch]$NoBuild,
  [string]$DllPath,
  [string]$InstallerPath,
  [string]$InstallerGuiPath
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$repoRootFull = [System.IO.Path]::GetFullPath($repoRoot)
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRootFull $BuildDir }
$buildPathFull = [System.IO.Path]::GetFullPath($buildPath)

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  $OutputDir = Join-Path $repoRootFull "dist\WavePreviewForExplorer-$Configuration"
}
$outputPathFull = [System.IO.Path]::GetFullPath($OutputDir)

function Resolve-PackageInput([string]$Path, [string]$DefaultPath) {
  if ([string]::IsNullOrWhiteSpace($Path)) {
    return [System.IO.Path]::GetFullPath($DefaultPath)
  }
  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }
  return [System.IO.Path]::GetFullPath((Join-Path $repoRootFull $Path))
}

$hasExplicitInputs = (-not [string]::IsNullOrWhiteSpace($DllPath)) -or
  (-not [string]::IsNullOrWhiteSpace($InstallerPath)) -or
  (-not [string]::IsNullOrWhiteSpace($InstallerGuiPath))
if ($hasExplicitInputs -and
    ([string]::IsNullOrWhiteSpace($DllPath) -or
     [string]::IsNullOrWhiteSpace($InstallerPath) -or
     [string]::IsNullOrWhiteSpace($InstallerGuiPath))) {
  throw "When using explicit package inputs, set -DllPath, -InstallerPath, and -InstallerGuiPath."
}

if (!$NoBuild -and !$hasExplicitInputs) {
  Write-Host "Building installer package inputs ($Configuration)"
  cmake --build $buildPathFull --config $Configuration --target WavePreviewShellExtension WavePreviewInstaller WavePreviewInstallerGui
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "Build failed. If WavePreviewShellExtension.dll is locked, close Explorer/prevhost or use scripts\rebuild-register-dev.ps1 for the dev cycle."
    exit $LASTEXITCODE
  }
}

$dllPath = Resolve-PackageInput $DllPath (Join-Path $buildPathFull "src\ShellExtension\$Configuration\WavePreviewShellExtension.dll")
$installerPath = Resolve-PackageInput $InstallerPath (Join-Path $buildPathFull "installer\native\$Configuration\WavePreviewInstaller.exe")
$installerGuiPath = Resolve-PackageInput $InstallerGuiPath (Join-Path $buildPathFull "installer\native\$Configuration\WavePreviewInstallerGui.exe")

if (!(Test-Path -LiteralPath $dllPath -PathType Leaf)) {
  throw "Shell extension DLL not found: $dllPath"
}
if (!(Test-Path -LiteralPath $installerPath -PathType Leaf)) {
  throw "Installer executable not found: $installerPath"
}
if (!(Test-Path -LiteralPath $installerGuiPath -PathType Leaf)) {
  throw "Installer GUI executable not found: $installerGuiPath"
}

New-Item -ItemType Directory -Force -Path $outputPathFull | Out-Null
Copy-Item -LiteralPath $dllPath -Destination (Join-Path $outputPathFull "WavePreviewShellExtension.dll") -Force
Copy-Item -LiteralPath $installerPath -Destination (Join-Path $outputPathFull "WavePreviewInstaller.exe") -Force
Copy-Item -LiteralPath $installerGuiPath -Destination (Join-Path $outputPathFull "WavePreviewInstallerGui.exe") -Force

$readme = @"
WavePreview for Explorer installer package

Graphical installer:
  .\WavePreviewInstallerGui.exe
  .\WavePreviewInstaller.exe

Install for current user:
  .\WavePreviewInstaller.exe install --restart-explorer

Install with preview options:
  .\WavePreviewInstaller.exe install --audio on --autoplay off --space-to-play on --restart-explorer

Change options after install:
  .\WavePreviewInstaller.exe configure --autoplay on
  .\WavePreviewInstaller.exe configure --autoplay off
  .\WavePreviewInstaller.exe configure --reset-options

Check status:
  .\WavePreviewInstaller.exe status

Uninstall:
  .\WavePreviewInstaller.exe uninstall --restart-explorer

This package registers HKCU per-user shell extension entries and installs files under:
  %LOCALAPPDATA%\WavePreviewForExplorer
"@

Set-Content -Path (Join-Path $outputPathFull "README-install.txt") -Value $readme -Encoding ASCII
Copy-Item -LiteralPath (Join-Path $repoRootFull "docs\installer.md") -Destination (Join-Path $outputPathFull "installer.md") -Force

Write-Host "Installer package created: $outputPathFull"
