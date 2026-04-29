[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [ValidateSet("On", "Off", "Default")]
  [string]$EnableAudio,

  [ValidateSet("On", "Off", "Default")]
  [string]$AutoPlay,

  [ValidateSet("On", "Off", "Default")]
  [string]$SpaceToPlay,

  [switch]$Reset
)

$ErrorActionPreference = "Stop"

$settingsKey = "Registry::HKEY_CURRENT_USER\Software\WavePreviewForExplorer\Preview"
$defaults = @{
  EnableAudio = 1
  AutoPlay = 0
  SpaceToPlay = 1
}

function Ensure-SettingsKey {
  if (!(Test-Path -LiteralPath $settingsKey)) {
    New-Item -Path $settingsKey -Force | Out-Null
  }
}

function Set-PreviewOption {
  param(
    [string]$Name,
    [string]$State
  )

  if ([string]::IsNullOrWhiteSpace($State)) {
    return
  }

  if ($State -eq "Default") {
    if (Test-Path -LiteralPath $settingsKey) {
      $existing = Get-ItemProperty -LiteralPath $settingsKey -Name $Name -ErrorAction SilentlyContinue
      if ($null -ne $existing -and $PSCmdlet.ShouldProcess($settingsKey, "Remove $Name override")) {
        Remove-ItemProperty -LiteralPath $settingsKey -Name $Name -Force
      }
    }
    return
  }

  Ensure-SettingsKey
  $value = if ($State -eq "On") { 1 } else { 0 }
  if ($PSCmdlet.ShouldProcess($settingsKey, "Set $Name to $value")) {
    New-ItemProperty -Path $settingsKey -Name $Name -Value $value -PropertyType DWord -Force | Out-Null
  }
}

function Get-EffectiveOption {
  param([string]$Name)

  if (Test-Path -LiteralPath $settingsKey) {
    $property = Get-ItemProperty -LiteralPath $settingsKey -Name $Name -ErrorAction SilentlyContinue
    if ($null -ne $property) {
      return [int]$property.$Name
    }
  }
  return [int]$defaults[$Name]
}

if ($Reset) {
  if (Test-Path -LiteralPath $settingsKey) {
    if ($PSCmdlet.ShouldProcess($settingsKey, "Remove all WavePreview preview option overrides")) {
      Remove-Item -LiteralPath $settingsKey -Recurse -Force
    }
  }
} else {
  Set-PreviewOption -Name "EnableAudio" -State $EnableAudio
  Set-PreviewOption -Name "AutoPlay" -State $AutoPlay
  Set-PreviewOption -Name "SpaceToPlay" -State $SpaceToPlay
}

Write-Host "WavePreview preview options:"
foreach ($name in @("EnableAudio", "AutoPlay", "SpaceToPlay")) {
  $value = Get-EffectiveOption -Name $name
  $state = if ($value -ne 0) { "On" } else { "Off" }
  Write-Host "  $name = $state"
}

Write-Host "Restart Explorer or reload the preview pane for running preview handlers to pick up changes."
