# Build

> CMake + Conan. Windows (VS2022 x64) is the primary target.

**Source:** `CMakeLists.txt`, `CMakePresets.json`, `conanfile.txt`

## Presets

From `CMakePresets.json`:
- `windows-default` — VS2022 x64, uses `build/conan_toolchain.cmake`
- Build presets: `windows-default-build` (Debug),
  `windows-default-build-release` (Release)
- Test preset: `windows-test`

## Targets

| Target | Kind | Purpose |
|---|---|---|
| `mib_backend` | STATIC library | Core: services, camera abstraction, processing |
| `mib_studio_qt` | WIN32 executable | Production app (hardware camera only) |
| `mock_studio_qt` | executable | Dev app with mock-camera GUI selector |
| `capture_processing_test` | executable | Console test harness (`src/tests/capture_processing_test.cpp`) |

`mib_backend` is linked by all three executables. Source is in
`src/backend/`, `src/camera/`, and `src/backend/playback/`.

## Commands

```bash
# Configure (once)
cmake --preset windows-default

# Build Debug
cmake --build build --config Debug

# Build Release
cmake --build build --preset windows-default-build-release

# Deploy Qt runtime (CMake auto-triggers post-build; can run manually)
windeployqt.exe --release build/Release/mib_studio_qt.exe
```

## Conan

Dependencies resolved via Conan (not vcpkg — despite old comments). See
[[Dependencies]] and `conanfile.txt`. Post-build hooks call
`windeployqt.exe` to copy Qt plugins and DLLs next to the exe.

## Related how-tos

- `docs/howto/build-installer.md`
- `docs/howto/windows-deploy.md`
- `docs/howto/runtime-deploy.md`
- `docs/howto/release-workflow.md`
