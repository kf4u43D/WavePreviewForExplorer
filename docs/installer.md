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

## Association ownership and repair

The installer records the exact extension and ProgID association keys it changes, together with their previous values, under `HKCU\Software\AudioPreviewForExplorer\Associations`. Reinstalling with different extensions or provider options restores the old associations before registering the requested ones. Uninstall reads this manifest even if the default application or ProgID has changed. If another application replaced an association after AudioPreview, its replacement is preserved.

For installations created by older versions without a manifest, cleanup removes only associations whose default value is still an AudioPreview CLSID. Previously overwritten third-party values from such installations cannot be reconstructed.

A failed installation restores the registry values and files captured before the operation. If Explorer restart was requested, it is attempted on both success and failure. Process termination is restricted to the current Windows session.

## Uninstall completion

Exit code `0` means cleanup completed. Exit code `1` reports an operation that could not complete; locked files retain the installation metadata so uninstall can be retried. No administrator-only reboot deletion is scheduled.

When uninstall runs from an installed executable, Windows keeps that executable locked. Exit code `3010` here means **final cleanup is pending until the installer closes**; it does not request a reboot. The installer starts the Windows PowerShell executable in a hidden process, waits for its own process to exit, then deletes only the running installer and removes its installation metadata. Cleanup errors are written to `uninstall-error.txt` in the install folder. If PowerShell cannot be started, uninstall returns an error and can be retried from a separate copy of the installer. Directories containing unrelated files are left intact.

The GUI loads the installed directory and extensions when opened. Its Uninstall button uses the recorded installation rather than edited install fields.

Development registration scripts now call the same native registration engine, so they also preserve previous handlers and support `-WhatIf`. Build `AudioPreviewInstaller.exe` first; `-BuildDir` and `-Configuration` select its build location. Development registration does not copy or remove the DLL.

Deferred cleanup holds the same per-user installation mutex as install/uninstall and verifies its pending token, installation generation and executable path before deleting anything. A successful reinstall changes the generation and cancels any older pending cleanup.
