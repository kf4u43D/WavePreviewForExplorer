[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string]$BuildDir = "build",
  [string]$Configuration = "Debug",
  [string]$DllPath,
  [string[]]$Extensions = @(".wav", ".wave"),
  [switch]$IncludePreviewHandler
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'installer-common.ps1')
$installer = Resolve-AudioPreviewInstaller -RepoRoot $repoRoot -BuildDir $BuildDir -Configuration $Configuration
$resolvedDll = Resolve-AudioPreviewDevDll -ExplicitPath $DllPath -RepoRoot $repoRoot -BuildDir $BuildDir -Configuration $Configuration
$preview = if ($IncludePreviewHandler) { 'on' } else { 'off' }

# Use the native transactional registration engine and its persistent association
# manifest; development registration must preserve previous handlers too.
if ($PSCmdlet.ShouldProcess("HKCU shell registration for $($Extensions -join ', ')", "Register $resolvedDll")) {
  & $installer register-dev --dll $resolvedDll --extensions ($Extensions -join ',') --thumbnail on --preview $preview
  if ($LASTEXITCODE -ne 0) { throw "Development registration failed (exit $LASTEXITCODE)." }
  Write-Host 'Registered development providers. Explorer was notified of the association change.'
}
