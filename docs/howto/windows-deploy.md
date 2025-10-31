# Windows: Deploy Qt runtime for running from PowerShell/File Explorer

Use windeployqt to copy required Qt DLLs and plugins next to the executable.

## Release build

Run:
```
powershell -NoProfile -Command "& 'D:\mib-studio-qt\build\vcpkg_installed\x64-windows\tools\Qt6\bin\windeployqt.exe' --release 'D:\mib-studio-qt\build\Release\mib_studio_qt.exe'"
```
Verify that `D:\mib-studio-qt\build\Release\platforms\qwindows.dll` exists, then launch:
```
D:\mib-studio-qt\build\Release\mib_studio_qt.exe
```

## Debug build
If your vcpkg triplet does not include debug Qt DLLs, windeploy for Debug may fail (e.g., missing `Qt6Widgetsd.dll`). Either:
- Build and install Qt debug binaries for your triplet, or
- Run the Release build for testing.

## Notes
- No env vars needed when plugins are deployed next to the exe.
- If you must run from a different folder, set `QT_QPA_PLATFORM_PLUGIN_PATH` to the `platforms` directory.
