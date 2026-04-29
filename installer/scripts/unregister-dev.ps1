[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string[]]$Extensions = @(".wav", ".wave")
)

$ErrorActionPreference = "Stop"

$previewClsid = "{7E0D2E0E-11D2-4D4A-8B75-7C94D1E76A01}"
$thumbnailClsid = "{57D1C278-A7A7-4D55-9859-4DA0B87117E2}"
$previewHandlerGuid = "{8895b1c6-b41f-4c1c-a562-0d564250836f}"
$thumbnailHandlerGuid = "{e357fccd-a995-4576-b01f-234630154e96}"
$classesRoot = "Registry::HKEY_CURRENT_USER\Software\Classes"
$approvedKey = "Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved"
$previewHandlersKey = "Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\PreviewHandlers"

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

function Remove-ShellAssociation {
  param(
    [string]$KeyPath,
    [string]$HandlerGuid,
    [string]$Description
  )

  $handlerKey = Join-Path $KeyPath "shellex\$HandlerGuid"
  if (Test-Path -LiteralPath $handlerKey) {
    if ($PSCmdlet.ShouldProcess($handlerKey, "Remove shell association for $Description")) {
      Remove-Item -LiteralPath $handlerKey -Recurse -Force
    }
  }
}

function Remove-RegistryValueIfPresent {
  param(
    [string]$Path,
    [string]$Name,
    [string]$Description
  )

  if (Test-Path -LiteralPath $Path) {
    $value = Get-ItemProperty -LiteralPath $Path -Name $Name -ErrorAction SilentlyContinue
    if ($null -ne $value) {
      if ($PSCmdlet.ShouldProcess($Path, "Remove $Description")) {
        Remove-ItemProperty -LiteralPath $Path -Name $Name -Force
      }
    }
  }
}

Write-Warning "Development unregister removes only WavePreview HKCU registry entries."

foreach ($extension in $Extensions) {
  $normalizedExtension = Normalize-Extension $extension
  $associationKeys = @(
    Join-Path $classesRoot $normalizedExtension
    Join-Path $classesRoot "SystemFileAssociations\$normalizedExtension"
  )

  $progId = Get-ProgIdForExtension $normalizedExtension
  if (![string]::IsNullOrWhiteSpace($progId)) {
    $associationKeys += Join-Path $classesRoot $progId
  }

  foreach ($associationKey in $associationKeys) {
    Remove-ShellAssociation -KeyPath $associationKey -HandlerGuid $thumbnailHandlerGuid -Description "$normalizedExtension thumbnail"
    Remove-ShellAssociation -KeyPath $associationKey -HandlerGuid $previewHandlerGuid -Description "$normalizedExtension preview"
  }
}

Remove-RegistryValueIfPresent -Path $approvedKey -Name $thumbnailClsid -Description "approved thumbnail shell extension entry"
Remove-RegistryValueIfPresent -Path $approvedKey -Name $previewClsid -Description "approved preview shell extension entry"
Remove-RegistryValueIfPresent -Path $previewHandlersKey -Name $previewClsid -Description "PreviewHandlers entry"

foreach ($clsid in @($thumbnailClsid, $previewClsid)) {
  $clsidKey = Join-Path $classesRoot "CLSID\$clsid"
  if (Test-Path -LiteralPath $clsidKey) {
    if ($PSCmdlet.ShouldProcess($clsidKey, "Remove COM registration")) {
      Remove-Item -LiteralPath $clsidKey -Recurse -Force
    }
  }
}

Write-Host "Unregistered development shell providers for: $($Extensions -join ', ')"
Write-Host "Restart Explorer or sign out/in if shell changes remain cached."
