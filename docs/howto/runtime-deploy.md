# Runtime deployment (Qt via vcpkg)

This project uses Qt from vcpkg. For Release builds we deploy the Qt runtime automatically using windeployqt from the same vcpkg toolchain.

## Key points

- Use the windeployqt that matches the Qt you linked with. Here, that is the vcpkg-installed `windeployqt.exe` under `vcpkg_installed/x64-windows/tools/qt6/bin` inside your build tree.
- Release builds are deployed automatically after linking; Debug builds are not deployed by default.
- If you need to run Debug binaries outside the build tree, copy required Qt debug DLLs from `vcpkg_installed/x64-windows/debug/bin` or install Qt debug runtime.

## Release workflow

1) Configure with vcpkg toolchain:

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake `
  -G "Visual Studio 17 2022"
```

2) Build Release (windeployqt runs post-build for each app target):

```powershell
cmake --build build --config Release --target mib_studio_qt mock_studio_qt
```

3) Find deployed apps next to the built executables (e.g., `build\bin\Release\mib_studio_qt.exe`). All required Qt DLLs and plugins will be copied alongside by windeployqt.

## Debug workflow (optional)

- Either run from your IDE (Qt debug DLLs are loaded from vcpkg build tree), or
- Copy needed debug DLLs from `build\vcpkg_installed\x64-windows\debug\bin` to the folder with your `.exe`.

## Notes

- The CMake step wires windeployqt like this (Release): it sets PATH to the vcpkg `bin`, then calls `windeployqt --release <exe>` for each app target.
- You can also package via CMake install + `qt_generate_deploy_app_script` (Qt 6.5+), if you prefer install-time deployment.
- If you need VC++ runtime DLLs in the bundle, make sure your environment includes them or pass `--compiler-runtime` to windeployqt.


