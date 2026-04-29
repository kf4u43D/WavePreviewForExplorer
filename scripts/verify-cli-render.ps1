$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$vsDevCmd = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$outDir = Join-Path $repoRoot "build\verify-cli-render"
$exePath = Join-Path $outDir "waveform-test-cli.exe"
$thumbnailExePath = Join-Path $outDir "thumbnail-smoke-cli.exe"
$comThumbnailExePath = Join-Path $outDir "com-thumbnail-smoke-cli.exe"
$previewExePath = Join-Path $outDir "preview-smoke-cli.exe"
$installerExePath = Join-Path $outDir "WavePreviewInstaller.exe"
$shellDllPath = Join-Path $outDir "WavePreviewShellExtension-smoke.dll"
$cmdPath = Join-Path $outDir "compile.cmd"
$wavPath = Join-Path $outDir "verify-input-pcm16.wav"
$bmpPath = Join-Path $outDir "verify-waveform.bmp"

if (!(Test-Path $vsDevCmd)) {
  throw "VsDevCmd.bat not found at $vsDevCmd. Install Visual Studio 2022 Build Tools with C++ support."
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$compileCommand = @"
@echo off
call "$vsDevCmd" -arch=x64 >nul
cl /nologo /std:c++20 /EHsc ^
  /I "$repoRoot\src\AudioEngine" ^
  /I "$repoRoot\src\Common" ^
  "$repoRoot\tools\waveform-test-cli\main.cpp" ^
  "$repoRoot\src\AudioEngine\Decoders\WavDecoder.cpp" ^
  "$repoRoot\src\AudioEngine\Render\WaveformBitmapRenderer.cpp" ^
  /Fo"$outDir\\" ^
  /Fe:"$exePath"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /DUNICODE /D_UNICODE /DNOMINMAX ^
  /I "$repoRoot\src\AudioEngine" ^
  /I "$repoRoot\src\Common" ^
  /I "$repoRoot\src\ShellExtension" ^
  "$repoRoot\tools\thumbnail-smoke-cli\main.cpp" ^
  "$repoRoot\src\ShellExtension\ComModule.cpp" ^
  "$repoRoot\src\ShellExtension\ShellStreamUtils.cpp" ^
  "$repoRoot\src\ShellExtension\ThumbnailProvider\BitmapConversion.cpp" ^
  "$repoRoot\src\ShellExtension\ThumbnailProvider\ThumbnailProvider.cpp" ^
  "$repoRoot\src\AudioEngine\Decoders\WavDecoder.cpp" ^
  "$repoRoot\src\AudioEngine\Render\WaveformBitmapRenderer.cpp" ^
  /Fo"$outDir\\" ^
  /Fe:"$thumbnailExePath" ^
  /link gdi32.lib ole32.lib
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /DUNICODE /D_UNICODE /DNOMINMAX ^
  /I "$repoRoot\src\Common" ^
  "$repoRoot\tools\com-thumbnail-smoke-cli\main.cpp" ^
  /Fo"$outDir\\" ^
  /Fe:"$comThumbnailExePath" ^
  /link ole32.lib gdi32.lib
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /DUNICODE /D_UNICODE /DNOMINMAX ^
  /I "$repoRoot\src\AudioEngine" ^
  /I "$repoRoot\src\Common" ^
  /I "$repoRoot\src\ShellExtension" ^
  "$repoRoot\tools\preview-smoke-cli\main.cpp" ^
  "$repoRoot\src\ShellExtension\ComModule.cpp" ^
  "$repoRoot\src\ShellExtension\ShellStreamUtils.cpp" ^
  "$repoRoot\src\ShellExtension\PreviewHandler\PreviewHandler.cpp" ^
  "$repoRoot\src\AudioEngine\Decoders\WavDecoder.cpp" ^
  "$repoRoot\src\AudioEngine\Render\WaveformBitmapRenderer.cpp" ^
  /Fo"$outDir\\" ^
  /Fe:"$previewExePath" ^
  /link ole32.lib user32.lib gdi32.lib winmm.lib advapi32.lib
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /DUNICODE /D_UNICODE /DNOMINMAX ^
  /I "$repoRoot\src\AudioEngine" ^
  /I "$repoRoot\src\Common" ^
  /I "$repoRoot\src\ShellExtension" ^
  /c "$repoRoot\src\ShellExtension\DllMain.cpp" ^
  /Fo"$outDir\DllMain.obj"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /LD /std:c++20 /EHsc /DUNICODE /D_UNICODE /DNOMINMAX ^
  /I "$repoRoot\src\AudioEngine" ^
  /I "$repoRoot\src\Common" ^
  /I "$repoRoot\src\ShellExtension" ^
  "$repoRoot\src\ShellExtension\DllMain.cpp" ^
  "$repoRoot\src\ShellExtension\ComModule.cpp" ^
  "$repoRoot\src\ShellExtension\ShellStreamUtils.cpp" ^
  "$repoRoot\src\ShellExtension\PreviewHandler\PreviewHandler.cpp" ^
  "$repoRoot\src\ShellExtension\ThumbnailProvider\BitmapConversion.cpp" ^
  "$repoRoot\src\ShellExtension\ThumbnailProvider\ThumbnailProvider.cpp" ^
  "$repoRoot\src\AudioEngine\Decoders\WavDecoder.cpp" ^
  "$repoRoot\src\AudioEngine\Render\WaveformBitmapRenderer.cpp" ^
  /Fo"$outDir\\" ^
  /Fe:"$shellDllPath" ^
  /link /DEF:"$repoRoot\src\ShellExtension\WavePreviewShellExtension.def" ole32.lib user32.lib gdi32.lib winmm.lib advapi32.lib
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /DUNICODE /D_UNICODE /DNOMINMAX ^
  "$repoRoot\installer\native\main.cpp" ^
  /Fo"$outDir\\" ^
  /Fe:"$installerExePath" ^
  /link advapi32.lib shell32.lib ole32.lib
if errorlevel 1 exit /b %errorlevel%
"@

Set-Content -Path $cmdPath -Value $compileCommand -Encoding ASCII
cmd.exe /d /s /c "`"$cmdPath`""
if ($LASTEXITCODE -ne 0) {
  throw "Direct MSVC compile failed with exit code $LASTEXITCODE"
}
if (!(Test-Path $exePath)) {
  throw "Direct MSVC compile finished but did not produce $exePath"
}
if (!(Test-Path $thumbnailExePath)) {
  throw "Direct MSVC compile finished but did not produce $thumbnailExePath"
}
if (!(Test-Path $comThumbnailExePath)) {
  throw "Direct MSVC compile finished but did not produce $comThumbnailExePath"
}
if (!(Test-Path $previewExePath)) {
  throw "Direct MSVC compile finished but did not produce $previewExePath"
}
if (!(Test-Path $shellDllPath)) {
  throw "Direct MSVC compile finished but did not produce $shellDllPath"
}
if (!(Test-Path $installerExePath)) {
  throw "Direct MSVC compile finished but did not produce $installerExePath"
}

$dumpbinCommand = "call `"$vsDevCmd`" -arch=x64 >nul && dumpbin /nologo /exports `"$shellDllPath`""
$exports = & cmd.exe /d /s /c $dumpbinCommand
foreach ($requiredExport in @("DllGetClassObject", "DllCanUnloadNow", "DllRegisterServer", "DllUnregisterServer")) {
  if (($exports | Select-String -SimpleMatch $requiredExport).Count -eq 0) {
    throw "Smoke DLL is missing export: $requiredExport"
  }
}

& $exePath --help
if ($LASTEXITCODE -ne 0) {
  throw "CLI --help failed with exit code $LASTEXITCODE"
}

& $installerExePath --help
if ($LASTEXITCODE -ne 0) {
  throw "Installer --help failed with exit code $LASTEXITCODE"
}

& $exePath (Join-Path $repoRoot "missing.wav")
if ($LASTEXITCODE -ne 2) {
  throw "CLI missing-file check returned $LASTEXITCODE instead of 2"
}

function Write-UInt16LE([System.IO.BinaryWriter]$Writer, [UInt16]$Value) {
  $Writer.Write($Value)
}

function Write-UInt32LE([System.IO.BinaryWriter]$Writer, [UInt32]$Value) {
  $Writer.Write($Value)
}

function Write-Int16LE([System.IO.BinaryWriter]$Writer, [Int16]$Value) {
  $Writer.Write($Value)
}

function Write-TestPcm16Wav([string]$Path) {
  $sampleRate = [UInt32]44100
  $channels = [UInt16]1
  $bitsPerSample = [UInt16]16
  $samples = [Int16[]](-32768, -16000, 0, 16000, 32767, 16000, 0, -16000)
  $blockAlign = [UInt16]($channels * ($bitsPerSample / 8))
  $byteRate = [UInt32]($sampleRate * $blockAlign)
  $dataBytes = [UInt32]($samples.Length * 2)

  $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
  try {
    $writer = [System.IO.BinaryWriter]::new($stream, [System.Text.Encoding]::ASCII, $true)
    try {
      $writer.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
      Write-UInt32LE $writer ([UInt32](36 + $dataBytes))
      $writer.Write([System.Text.Encoding]::ASCII.GetBytes("WAVE"))
      $writer.Write([System.Text.Encoding]::ASCII.GetBytes("fmt "))
      Write-UInt32LE $writer 16
      Write-UInt16LE $writer 1
      Write-UInt16LE $writer $channels
      Write-UInt32LE $writer $sampleRate
      Write-UInt32LE $writer $byteRate
      Write-UInt16LE $writer $blockAlign
      Write-UInt16LE $writer $bitsPerSample
      $writer.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
      Write-UInt32LE $writer $dataBytes
      foreach ($sample in $samples) {
        Write-Int16LE $writer $sample
      }
    } finally {
      $writer.Dispose()
    }
  } finally {
    $stream.Dispose()
  }
}

Write-TestPcm16Wav $wavPath
if (Test-Path $bmpPath) {
  Remove-Item -LiteralPath $bmpPath -Force
}

& $exePath $wavPath --render-bmp $bmpPath --width 128 --height 64
if ($LASTEXITCODE -ne 0) {
  throw "CLI render check failed with exit code $LASTEXITCODE"
}
if (!(Test-Path $bmpPath)) {
  throw "CLI render check did not produce $bmpPath"
}
if ((Get-Item $bmpPath).Length -le 54) {
  throw "Rendered BMP is too small to be valid"
}

$bmpHeader = [System.IO.File]::ReadAllBytes($bmpPath)[0..1]
if ($bmpHeader[0] -ne [byte][char]'B' -or $bmpHeader[1] -ne [byte][char]'M') {
  throw "Rendered file does not have a BMP header"
}

& $thumbnailExePath $wavPath 128
if ($LASTEXITCODE -ne 0) {
  throw "Thumbnail smoke check failed with exit code $LASTEXITCODE"
}

& $previewExePath $wavPath
if ($LASTEXITCODE -ne 0) {
  throw "Preview smoke check failed with exit code $LASTEXITCODE"
}

Write-Host "CLI/render verification completed: $exePath"
exit 0
