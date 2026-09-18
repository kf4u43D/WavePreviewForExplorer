# Validation

Build and run all automated tests on Windows:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure --no-tests=error
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

With WPV_BUILD_TESTS=ON, GoogleTest is required; configuration fails if it is missing.
The CI uses the vcpkg baseline from vcpkg.json and executes both configurations.

## Automated coverage

- Shared file/memory WAV validation, PCM/float/extensible formats, truncated and
  inconsistent RIFF chunks, arithmetic limits, cancellation and PCM output limits.
- Waveform rendering, persistent cache invalidation/corruption handling, optional
  cache failures, entry/byte limits and bounded directory enumeration.
- COM previews from file, stream and IShellItem; full paths outside the working
  directory; deferred audio conversion; selection changes and stale results;
  Unload during slow IO; external parent destruction and recreation.
- Window class lifetime, including actual DLL load/unload cycles. These tests use
  hidden test windows and never register the DLL in Explorer or play sound.
- Installer association backup/restore, third-party replacement, custom extensions
  and directories, changed ProgIDs, rollback, locked files and deferred cleanup.
  Registry writes use dedicated disposable HKCU test roots. Explorer is not stopped.
  Deferred cleanup script generation is tested; the production cleanup helper is
  not launched against an existing installation.

## Manual validation still required

Use a disposable Windows account or VM for installation and Explorer tests:

- Install, update and uninstall with another audio preview provider present.
- Exercise the installed uninstaller, including deferred removal of its own EXE.
- Play/stop, auto-play, multiple Explorer windows and audio device changes.
- Rapidly select large WAV files on a slow/network drive; close/reopen the preview pane.
- Check DPI scaling, keyboard behavior and Windows themes.
- Compare a trusted local WAV with a downloaded copy carrying Zone.Identifier.
  The application does not change Windows Internet-zone policies or automatically
  remove the file's origin marker.

A worker can cancel between reads; an IO operation already blocked in Windows or
a third-party IStream may finish later. The preview object and its window can be
released immediately while the worker safely retains its own state and module.