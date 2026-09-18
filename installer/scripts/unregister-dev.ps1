[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string[]]$Extensions,
  [string]$BuildDir = "build",
  [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'installer-common.ps1')
$installer = Resolve-AudioPreviewInstaller -RepoRoot $repoRoot -BuildDir $BuildDir -Configuration $Configuration
$arguments = @('unregister-dev')
if ($PSBoundParameters.ContainsKey('Extensions')) { $arguments += @('--extensions', ($Extensions -join ',')) }

if ($PSCmdlet.ShouldProcess('HKCU AudioPreview shell registration', 'Restore previous associations recorded at installation')) {
  & $installer @arguments
  if ($LASTEXITCODE -ne 0) { throw "Development unregistration failed (exit $LASTEXITCODE)." }
  Write-Host 'Unregistered development providers and restored their previous associations.'
}
