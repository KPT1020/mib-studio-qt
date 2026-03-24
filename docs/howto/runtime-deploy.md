# Runtime deployment (Qt via Conan)

This project uses Qt from Conan. For Release builds we deploy the Qt runtime automatically using windeployqt from the same Conan toolchain.

## Key points

- Use the windeployqt that matches the Qt you linked with. Conan installs Qt tools in the package installation directory (typically under `CMAKE_PREFIX_PATH/tools/qt6/bin` or `CMAKE_PREFIX_PATH/bin`).
- Release builds are deployed automatically after linking; Debug builds are not deployed by default.
- If you need to run Debug binaries outside the build tree, copy required Qt debug DLLs from the Conan Qt installation directory or install Qt debug runtime.

## Release workflow

1) Create Conan profile (first time only):

```powershell
conan profile detect --force
```

Note: If the detected profile uses C++14 but your project requires C++17, edit the profile at `%USERPROFILE%\.conan2\profiles\default` and change `compiler.cppstd=14` to `compiler.cppstd=17`.

2) Install Conan dependencies and configure:

**CRITICAL**: For Visual Studio multi-config generators, you **MUST install both Debug and Release packages BEFORE configuring CMake**. Installing them separately or after configuration can cause multi-config path resolution issues.

```powershell
# Install both configurations BEFORE configuring CMake
conan install . --output-folder=build --build=missing -s build_type=Debug
conan install . --output-folder=build --build=missing -s build_type=Release
# Then configure (CMakeDeps will see both packages)
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -G "Visual Studio 17 2022"
```

**Why this order matters**: Each `conan install` regenerates `conan_toolchain.cmake` with paths for that build_type only. If you install Debug first, then Release, the toolchain file gets overwritten with Release paths. Installing both before configuring ensures CMake's imported targets (via CMakeDeps) can properly resolve paths for both configurations.

**Important**: For Debug builds, you must install Debug packages with `-s build_type=Debug`. Without this, Qt debug DLLs (e.g., `Qt6Chartsd.dll`) will not be available and windeployqt will fail.

Or use CMake presets:

```powershell
cmake --preset windows-default
```

2) Build Release (windeployqt runs post-build for each app target):

```powershell
cmake --build build --config Release --target mib_studio_qt
```

3) Find deployed apps next to the built executables (e.g., `build\bin\Release\mib_studio_qt.exe`). All required Qt DLLs and plugins will be copied alongside by windeployqt.

## Debug workflow (optional)

- Either run from your IDE (Qt debug DLLs are loaded from Conan installation), or
- Copy needed debug DLLs from the Conan Qt installation directory to the folder with your `.exe`.

## Notes

- The CMake step wires windeployqt like this (Release): it sets PATH to the Conan Qt `bin` directory, then calls `windeployqt --release <exe>` for each app target.
- The `CONAN_QT_BIN_DIR` detection in `CMakeLists.txt` uses CMake imported targets first (which handle multi-config properly), then falls back to searching `CMAKE_PREFIX_PATH`. This helps avoid issues when packages are installed incrementally.
- You can also package via CMake install + `qt_generate_deploy_app_script` (Qt 6.5+), if you prefer install-time deployment.
- If you need VC++ runtime DLLs in the bundle, make sure your environment includes them or pass `--compiler-runtime` to windeployqt.

## Troubleshooting

If you encounter errors about missing Qt DLLs when building:

1. **Verify both packages are installed**: Check that both Debug and Release Conan packages are installed:
   ```powershell
   conan list "*" --output-folder=build
   ```

2. **Reinstall from scratch**: If you installed packages incrementally and are having issues, delete the build directory and reinstall both packages before configuring:
   ```powershell
   Remove-Item -Recurse -Force build
   conan install . --output-folder=build --build=missing -s build_type=Debug
   conan install . --output-folder=build --build=missing -s build_type=Release
   cmake --preset windows-default
   ```

3. **Check CMake warnings**: The build system will warn if required DLLs are missing for the current configuration.
