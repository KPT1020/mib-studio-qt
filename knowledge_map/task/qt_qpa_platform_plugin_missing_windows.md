Title: Fix Qt platform plugin "windows" missing when running from PowerShell

Context
- Error: `qt.qpa.plugin: Could not find the Qt platform plugin "windows" in ""` when launching `mib_studio_qt.exe`.
- Cause: Qt runtime/plugins not deployed next to the exe when running outside the build system.

Resolution
- Used windeployqt from Conan to deploy runtime for Release:
  - Find windeployqt in Conan Qt installation (typically `CMAKE_PREFIX_PATH/tools/qt6/bin/windeployqt.exe`)
  - Run: `windeployqt.exe --release D:\mib-studio-qt\build\Release\mib_studio_qt.exe`
  - Verified `D:\mib-studio-qt\build\Release\platforms\qwindows.dll` exists.
- Debug deployment failed due to missing Qt debug DLLs in Conan Qt package (`Qt6Widgetsd.dll`). Prefer Release for now.

Follow-ups
- Add a short how-to in `docs/howto/windows-deploy.md`.
- Optional: add a CMake post-build step to run windeployqt automatically.
