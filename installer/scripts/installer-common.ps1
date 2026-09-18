function Resolve-AudioPreviewInstaller {
  param([string]$RepoRoot, [string]$BuildDir, [string]$Configuration)

  $buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $RepoRoot $BuildDir }
  $candidates = @(
    (Join-Path $buildPath "installer\native\$Configuration\AudioPreviewInstaller.exe"),
    (Join-Path $buildPath "installer\native\AudioPreviewInstaller.exe"),
    (Join-Path $RepoRoot 'AudioPreviewInstaller.exe')
  )
  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
      return [System.IO.Path]::GetFullPath($candidate)
    }
  }
  throw "AudioPreviewInstaller.exe not found. Build the native installer with scripts/full-build.ps1, or set -BuildDir and -Configuration."
}

function Resolve-AudioPreviewDevDll {
  param([string]$ExplicitPath, [string]$RepoRoot, [string]$BuildDir, [string]$Configuration)

  if (![string]::IsNullOrWhiteSpace($ExplicitPath)) {
    $resolved = (Resolve-Path -LiteralPath $ExplicitPath).Path
    if (!(Test-Path -LiteralPath $resolved -PathType Leaf)) { throw "DLL not found: $resolved" }
    return [System.IO.Path]::GetFullPath($resolved)
  }
  $buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $RepoRoot $BuildDir }
  foreach ($name in @('AudioPreviewShellExtension.dll', 'WavePreviewShellExtension.dll')) {
    foreach ($relative in @("src\ShellExtension\$Configuration\$name", "src\ShellExtension\$name")) {
      $candidate = Join-Path $buildPath $relative
      if (Test-Path -LiteralPath $candidate -PathType Leaf) { return [System.IO.Path]::GetFullPath($candidate) }
    }
  }
  throw "AudioPreviewShellExtension.dll not found. Build the project or pass -DllPath."
}
