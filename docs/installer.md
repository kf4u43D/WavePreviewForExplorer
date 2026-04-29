# AudioPreview Installer

AudioPreviewInstaller.exe installs AudioPreview for the current Windows user only. It does not require administrator rights because it writes HKCU registry entries and installs files under:

```text
%LOCALAPPDATA%\AudioPreviewForExplorer
```

## Graphical Install

Run:

```powershell
.\AudioPreviewInstallerGui.exe
```

The console installer can also open the same window:

```powershell
.\AudioPreviewInstaller.exe
.\AudioPreviewInstaller.exe gui
```

The installer window lets you choose:

- the shell extension DLL to install;
- the install folder;
- registered extensions, default `.wav,.wave`;
- preview handler and thumbnail provider registration;
- Explorer restart during install/uninstall;
- audio playback;
- auto-play;
- Space key Play/Stop.

Use `Install` to copy the DLL and register Explorer integration. Use `Apply options` to change audio options after install. Use `Status` to inspect the active registration. Use `Uninstall` to remove AudioPreview HKCU shell entries.

## Command Line

Install:

```powershell
.\AudioPreviewInstaller.exe install --audio on --autoplay off --space-to-play on --restart-explorer
```

Configure:

```powershell
.\AudioPreviewInstaller.exe configure --autoplay on
.\AudioPreviewInstaller.exe configure --autoplay off
.\AudioPreviewInstaller.exe configure --reset-options
```

Status:

```powershell
.\AudioPreviewInstaller.exe status
```

Uninstall:

```powershell
.\AudioPreviewInstaller.exe uninstall --restart-explorer
```

## Notes

Explorer can keep shell extension DLLs loaded. If install or uninstall cannot replace/remove the DLL, close Explorer windows, stop `prevhost.exe`, or use the installer option to restart Explorer.

Auto-play is off by default. This is intentional because Explorer previews files as soon as selection changes.
