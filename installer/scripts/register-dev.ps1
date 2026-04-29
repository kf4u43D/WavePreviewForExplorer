[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string]$BuildDir = "build",
  [string]$Configuration = "Debug",
  [string]$DllPath,
  [string[]]$Extensions = @(".wav", ".wave"),
  [switch]$IncludePreviewHandler
)

$ErrorActionPreference = "Stop"

$previewClsid = "{7E0D2E0E-11D2-4D4A-8B75-7C94D1E76A01}"
$thumbnailClsid = "{57D1C278-A7A7-4D55-9859-4DA0B87117E2}"
$previewHandlerGuid = "{8895b1c6-b41f-4c1c-a562-0d564250836f}"
$thumbnailHandlerGuid = "{e357fccd-a995-4576-b01f-234630154e96}"
$prevhostAppId = "{6d2b5079-2f0b-48dd-ab7f-97cec514d30b}"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$classesRoot = "Registry::HKEY_CURRENT_USER\Software\Classes"
$approvedKey = "Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved"
$previewHandlersKey = "Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\PreviewHandlers"

function Resolve-DevDllPath {
  param(
    [string]$ExplicitPath,
    [string]$RepoRoot,
    [string]$BuildDir,
    [string]$Configuration
  )

  if (![string]::IsNullOrWhiteSpace($ExplicitPath)) {
    $resolved = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $ExplicitPath).Path)
    if (!(Test-Path -LiteralPath $resolved -PathType Leaf)) {
      throw "DLL not found: $resolved"
    }
    return $resolved
  }

  $buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $RepoRoot $BuildDir }
  $candidate = Join-Path $buildPath "src\ShellExtension\$Configuration\AudioPreviewShellExtension.dll"
  if (Test-Path -LiteralPath $candidate -PathType Leaf) {
    return [System.IO.Path]::GetFullPath($candidate)
  }

  $legacyCandidate = Join-Path $buildPath "src\ShellExtension\$Configuration\WavePreviewShellExtension.dll"
  if (Test-Path -LiteralPath $legacyCandidate -PathType Leaf) {
    return [System.IO.Path]::GetFullPath($legacyCandidate)
  }

  $found = Get-ChildItem -LiteralPath $buildPath -Include AudioPreviewShellExtension.dll,WavePreviewShellExtension.dll -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if ($null -ne $found) {
    return $found.FullName
  }

  throw "AudioPreviewShellExtension.dll not found under $buildPath. Run scripts/full-build.ps1 first or pass -DllPath."
}

function Set-DefaultRegistryValue {
  param(
    [string]$Path,
    [string]$Value
  )

  if (!(Test-Path -LiteralPath $Path)) {
    New-Item -Path $Path -Force | Out-Null
  }
  Set-Item -Path $Path -Value $Value
}

function Normalize-Extension {
  param([string]$Extension)

  if ([string]::IsNullOrWhiteSpace($Extension)) {
    throw "Empty extension is not allowed"
  }
  if (!$Extension.StartsWith(".")) {
    return ".$Extension"
  }
  return $Extension
}

function Get-ProgIdForExtension {
  param([string]$Extension)

  $extensionKey = "Registry::HKEY_CLASSES_ROOT\$Extension"
  $key = Get-Item -LiteralPath $extensionKey -ErrorAction SilentlyContinue
  if ($null -eq $key) {
    return $null
  }

  $progId = $key.GetValue("")
  if ([string]::IsNullOrWhiteSpace($progId)) {
    return $null
  }
  return [string]$progId
}

function Register-ComServer {
  param(
    [string]$Clsid,
    [string]$Name,
    [string]$DllPath,
    [string]$AppId
  )

  $clsidKey = Join-Path $classesRoot "CLSID\$Clsid"
  $inprocKey = Join-Path $clsidKey "InprocServer32"
  if ($PSCmdlet.ShouldProcess($inprocKey, "Register COM in-process server for $Name")) {
    Set-DefaultRegistryValue -Path $clsidKey -Value $Name
    Set-DefaultRegistryValue -Path $inprocKey -Value $DllPath
    New-ItemProperty -Path $inprocKey -Name "ThreadingModel" -Value "Apartment" -PropertyType String -Force | Out-Null
    if (![string]::IsNullOrWhiteSpace($AppId)) {
      New-ItemProperty -Path $clsidKey -Name "AppID" -Value $AppId -PropertyType String -Force | Out-Null
    }
  }
}

function Register-ShellAssociation {
  param(
    [string]$KeyPath,
    [string]$HandlerGuid,
    [string]$Clsid,
    [string]$Description
  )

  $handlerKey = Join-Path $KeyPath "shellex\$HandlerGuid"
  if ($PSCmdlet.ShouldProcess($handlerKey, "Associate $Description")) {
    Set-DefaultRegistryValue -Path $handlerKey -Value $Clsid
  }
}

$resolvedDllPath = Resolve-DevDllPath -ExplicitPath $DllPath -RepoRoot $repoRoot -BuildDir $BuildDir -Configuration $Configuration

Write-Warning "Development registration writes HKCU only. Do not use this as a production installer."
Write-Host "Registering shell extension DLL: $resolvedDllPath"

Register-ComServer -Clsid $thumbnailClsid -Name "AudioPreview Thumbnail Provider" -DllPath $resolvedDllPath
if ($IncludePreviewHandler) {
  Register-ComServer -Clsid $previewClsid -Name "AudioPreview Preview Handler" -DllPath $resolvedDllPath -AppId $prevhostAppId
}

if ($PSCmdlet.ShouldProcess($approvedKey, "Approve development shell extension")) {
  if (!(Test-Path -LiteralPath $approvedKey)) {
    New-Item -Path $approvedKey -Force | Out-Null
  }
  New-ItemProperty -Path $approvedKey -Name $thumbnailClsid -Value "AudioPreview Thumbnail Provider" -PropertyType String -Force | Out-Null
  if ($IncludePreviewHandler) {
    New-ItemProperty -Path $approvedKey -Name $previewClsid -Value "AudioPreview Preview Handler" -PropertyType String -Force | Out-Null
  }
}

if ($IncludePreviewHandler) {
  if ($PSCmdlet.ShouldProcess($previewHandlersKey, "Register PreviewHandlers entry")) {
    if (!(Test-Path -LiteralPath $previewHandlersKey)) {
      New-Item -Path $previewHandlersKey -Force | Out-Null
    }
    New-ItemProperty -Path $previewHandlersKey -Name $previewClsid -Value "AudioPreview Preview Handler" -PropertyType String -Force | Out-Null
  }
}

foreach ($extension in $Extensions) {
  $normalizedExtension = Normalize-Extension $extension
  $associationKeys = @(
    Join-Path $classesRoot $normalizedExtension
    Join-Path $classesRoot "SystemFileAssociations\$normalizedExtension"
  )

  $progId = Get-ProgIdForExtension $normalizedExtension
  if (![string]::IsNullOrWhiteSpace($progId)) {
    $associationKeys += Join-Path $classesRoot $progId
    Write-Host "Detected $normalizedExtension ProgID: $progId"
  }

  foreach ($associationKey in $associationKeys) {
    Register-ShellAssociation -KeyPath $associationKey -HandlerGuid $thumbnailHandlerGuid -Clsid $thumbnailClsid -Description "$normalizedExtension thumbnail"
    if ($IncludePreviewHandler) {
      Register-ShellAssociation -KeyPath $associationKey -HandlerGuid $previewHandlerGuid -Clsid $previewClsid -Description "$normalizedExtension preview"
    }
  }
}

Write-Host "Registered development thumbnail provider for: $($Extensions -join ', ')"
if ($IncludePreviewHandler) {
  Write-Host "Registered development preview handler for: $($Extensions -join ', ')"
}
Write-Host "Restart Explorer or sign out/in if shell changes do not refresh."
