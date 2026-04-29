param(
  [string]$Clsid = "{57D1C278-A7A7-4D55-9859-4DA0B87117E2}"
)

$ErrorActionPreference = "Stop"

$vsDevCmd = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$inprocKey = "Registry::HKEY_CURRENT_USER\Software\Classes\CLSID\$Clsid\InprocServer32"

if (!(Test-Path -LiteralPath $inprocKey)) {
  throw "HKCU COM registration not found: $inprocKey"
}

$key = Get-Item -LiteralPath $inprocKey
$dllPath = [string]$key.GetValue("")
$threadingModel = [string]$key.GetValue("ThreadingModel")
$clsidKey = "Registry::HKEY_CURRENT_USER\Software\Classes\CLSID\$Clsid"
$clsidItem = Get-Item -LiteralPath $clsidKey
$appId = [string]$clsidItem.GetValue("AppID")

Write-Host "CLSID: $Clsid"
Write-Host "DLL: $dllPath"
Write-Host "ThreadingModel: $threadingModel"
if (![string]::IsNullOrWhiteSpace($appId)) {
  Write-Host "AppID: $appId"
}

if (!(Test-Path -LiteralPath $dllPath -PathType Leaf)) {
  throw "Registered DLL does not exist: $dllPath"
}

if (!(Test-Path -LiteralPath $vsDevCmd)) {
  Write-Warning "VsDevCmd.bat not found; skipping dumpbin export check."
  exit 0
}

$dumpbinCommand = "call `"$vsDevCmd`" -arch=x64 >nul && dumpbin /nologo /exports `"$dllPath`""
$exports = & cmd.exe /d /s /c $dumpbinCommand

foreach ($requiredExport in @("DllGetClassObject", "DllCanUnloadNow", "DllRegisterServer", "DllUnregisterServer")) {
  if (($exports | Select-String -SimpleMatch $requiredExport).Count -eq 0) {
    throw "Registered DLL is missing export: $requiredExport"
  }
}

Write-Host "Registered DLL exports are present."
