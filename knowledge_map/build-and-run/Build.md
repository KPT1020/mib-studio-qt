# Build

> CMake + Conan. Windows (VS2022 x64) is the primary target, with Linux
> cloud builds supported for non-hardware paths.

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
| `mib_studio_qt` | executable (`WIN32` on Windows) | Production app |
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
`windeployqt.exe` to copy Qt plugins and DLLs next to the exe. CMake resolves
`windeployqt` and the `PATH` prefix from Conan CMakeDeps’ `qt_PACKAGE_FOLDER_*`
so Release/Debug tools stay aligned with the linked Qt package (stale
`find_program` cache or cache-wide `Qt6Core.dll` globs could otherwise mix
different Conan package IDs after reinstalls).

## Platform guards (hardware SDKs)

- `CMakeLists.txt` sets `MIB_HAS_EGRABBER`:
  - `ON` on Windows
  - `OFF` on non-Windows
- When `MIB_HAS_EGRABBER=OFF`, build wiring skips:
  - EGrabber include path (`C:/Program Files/Euresys/eGrabber/include`)
  - Coremor include path (`include/Coremor`)
  - Coremor import library (`XMT_DLL_SER.lib`)
  - Windows-only autofocus implementation (`AutofocusService.cpp`)
- Non-Windows uses `src/backend/services/AutofocusService.stub.cpp` so Linux
  cloud builds can compile and run mock/non-hardware workflows.

## Linux cloud toolchain note (`cannot find -lstdc++`)

Some cloud images can fail during compiler smoke-test before project
configuration with:

`/usr/bin/ld: cannot find -lstdc++`

In those cases, `/usr/bin/c++` is often set to `clang++` via alternatives while
the image lacks the expected unversioned `libstdc++.so` path for that clang
setup.

Workaround:

```bash
sudo update-alternatives --set c++ /usr/bin/g++
printf 'int main(){return 0;}' | c++ -x c++ - -o /tmp/cxx-link-test
```

If this succeeds, rerun CMake/Conan. Any next failure is likely dependency
resolution/provisioning, not the runtime linker.

## Linux cloud dependency fallback (ONNX Runtime optional)

Linux cloud images may not have a discoverable CMake package for ONNX Runtime
(`onnxruntimeConfig.cmake`), and Conan graph resolution can fail because of
upstream version conflicts (`qt/opencv/onnxruntime` transitive deps).

To keep non-hardware workflows buildable in cloud:

- `find_package(onnxruntime CONFIG QUIET)` is optional.
- `MIB_HAS_ONNXRUNTIME` is set from `TARGET onnxruntime::onnxruntime`.
- When ONNX Runtime is unavailable:
  - build uses `src/backend/services/YoloService.stub.cpp`
  - compile definition `MIB_HAS_ONNXRUNTIME=0` is exported
  - CMake emits a warning and continues.

## Related how-tos

- `docs/howto/build-installer.md`
- `docs/howto/windows-deploy.md`
- `docs/howto/runtime-deploy.md`
- `docs/howto/release-workflow.md`
