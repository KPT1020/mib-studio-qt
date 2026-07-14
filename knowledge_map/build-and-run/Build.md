# Build

> CMake + Conan. Windows (VS2022 x64) is the primary target, with Linux
> cloud builds supported for non-hardware paths.

**Source:** `CMakeLists.txt`, `CMakePresets.json`, `conanfile.py`

## Presets

From `CMakePresets.json`:
- `windows-default` — VS2022 x64, uses `build/conan_toolchain.cmake`
- `linux-backend-only` — Linux backend-only configure (`mib_backend` + tests;
  skips frontend executables)
- Build presets: `windows-default-build` (Debug),
  `windows-default-build-release` (Release),
  `linux-backend-only-build`
- Test presets: `windows-test`, `linux-backend-only-test`

## Targets

| Target | Kind | Purpose |
|---|---|---|
| `mib_processing` | STATIC library | Qt-free processing core: `ProcessingService`, `EModulusLut`, `BatchMaskSources`, `Hdf5Service`, `FrameStore`, `Tools`, `CrashStateMirror`. Links only OpenCV + HDF5 + spdlog + STL. |
| `mib_processing_core` | MODULE library | Native hot-swap plugin exposing the C engine ABI. Release Windows output is `build/Release/mib_processing_core-<version>-windows_x86_64.dll` plus the same-stem descriptor JSON. |
| `mib_backend` | STATIC library | Core: services, camera abstraction; links `mib_processing` publicly |
| `mib_frontend_common` | STATIC library | All UI sources (tabs, dialogs, `.ui` files) compiled once and linked by both frontend executables. AUTOUIC runs only here — it is `OFF` on the executables because CMake would emit duplicate `ui_*.h` generation rules per target; the `.qrc` is compiled per-executable so the Qt resources survive static linking |
| `mib_studio_qt` | executable (`WIN32` on Windows) | Production app (mock camera reachable via ConnectTab "Configure Mock…" or `MIB_CAMERA_MODE=mock`) |
| `screenshot_tour` | executable | Headless UI tour that regenerates the user-manual screenshots (`docs/manual/images`); builds on Linux too (`linux-system-release`); see [[../frontend/Screenshot-Tour]] |
| `mib_backend_smoke_test` | executable test | Backend-only HDF5/open/flush smoke test (`ctest -L backend`) |
| `emodulus_lut_catalog_test` | executable test | Backend-only LUT manifest/cache smoke test (`ctest -L backend`) |

`mib_backend` is linked by every executable. Source is in
`src/backend/`, `src/camera/`, and `src/backend/playback/`.

`mib_processing` is defined first in `src/backend/CMakeLists.txt` and has
`AUTOMOC`/`AUTOUIC`/`AUTORCC` explicitly off — it must not require the Qt
`moc` toolchain to build standalone. `backend-ci.yml` builds it explicitly
and greps its symbols to fail CI if a Qt dependency leaks back in. This is
the artifact a non-Qt consumer (Biowork's `services/mib-processing`) is
meant to build/bind against — see `docs/gold_standard_metrics.md` ("Portable
Processing Contract") and the [Biowork portability
epic](https://github.com/KPT1020/mib-studio-qt/issues/220).

Backend-only builds set `MIB_BUILD_BACKEND_ONLY=ON` and skip frontend target
generation entirely, but still require Qt `Core+Gui+SerialPort+Network`
because the backend now fetches the LUT manifest directly at startup.

## Python bindings (`bindings/python/`)

`_mib_processing` is a pybind11 extension module (`bindings/python/src/`)
linking `mib_processing`; the importable package is `mib_processing`
(`bindings/python/python/mib_processing/`, thin re-export layer). Built via
[scikit-build-core](https://scikit-build-core.readthedocs.io), driven by
`bindings/python/pyproject.toml`, which points `cmake.source-dir` at the
repo root so it configures the *same* root `CMakeLists.txt` (option
`MIB_BUILD_PYTHON_BINDINGS=ON`, set automatically by the wheel build) rather
than a separate CMake project:

```bash
cd bindings/python
pip install .              # or: pip install -e . --no-build-isolation (dev loop)
python -m pytest tests/
cd ../..
python scripts/run_processing_conformance.py  # installed-wheel anti-drift check
```

Requires the same system packages as `linux-backend-only`/
`linux-system-release` (`docs/howto/linux-build.md`) — including Qt, since
`add_subdirectory(src/backend)` still configures `mib_backend` alongside
`mib_processing` even though the wheel only installs the latter. `mib_processing`
has `POSITION_INDEPENDENT_CODE ON` (needed to link a static library into a
shared `.so` extension module); this has no effect on the desktop static/
executable link.

Not an auditwheel/manylinux-portable wheel — see `bindings/python/README.md`.
CI: `.github/workflows/python-wheel.yml` builds + tests on every relevant PR
then runs the full-parity conformance harness before publishing wheels as
GitHub Release assets on `mib-processing-v*` tags
(a separate tag namespace from the app's own `v*.*.*` releases,
`.github/workflows/release.yml`).

The same workflow is the processing-core release gate. Its wheel matrix covers
CPython 3.10–3.13, a Windows x64 job builds the native core artifact, and a tag
release attaches all assets before `publish-processing-core.py --from-release`
updates the R2 registry. Publication order is immutable
`processing-core/versions/<version>.json`, merged `index.json`, generated PEP
503 page, then the backward-compatible full `latest.json` pointer. A tag build
fails when production Authenticode or R2 secrets are absent; local/PR builds
may exercise the unsigned fixture path but cannot publish it.

Use `python scripts/bump_mib_processing_version.py <version>` to update both
the authoritative pyproject and package wrapper literal. After committing,
rerun with `--create-tag`; the script verifies that `HEAD` contains the version
before creating `mib-processing-v<version>`.

## Native processing-core plugin

`mib_processing_core` compiles only the bundled mask/empty-frame kernel and C
ABI adapter; it does not link Qt, HDF5, or the desktop service graph. Its
version comes from the same `bindings/python/pyproject.toml` literal as the
wheel. On Windows, CMake emits a same-stem descriptor containing the exact
version, ABI/contract, `mib_processing_get_api` entrypoint, runtime
fingerprint, compatible app range, and required Authenticode scheme.

Release CI runs the ABI, loader, cache, and activation tests, signs the DLL,
attaches the signed DLL/descriptor beside the wheels, then lets the registry
publisher calculate the final byte size and SHA-256. The desktop rechecks the
registry digest and compiled Authenticode signer-SPKI allowlist immediately
before `LoadLibraryExW`; unsigned debug fixtures are never publishable.

The C ABI contract is in
`include/backend/processing/ProcessingCoreAbi.h`. Keep it POD-only and
append-compatible: no STL/OpenCV/Qt objects, exceptions, RTTI, or ownership
transfer may cross this boundary. `tests/processing/processing_core_abi_c_test.c`
is deliberately compiled as C, while loader parity and cache/concurrency tests
exercise the dynamic artifact.

Production desktop builds must configure
`-DMIB_REQUIRE_PROCESSING_CORE_SIGNER_SPKI=ON` and
`-DMIB_PROCESSING_CORE_SIGNER_SPKI_SHA256=<64-hex>` with the approved signer's
DER SubjectPublicKeyInfo SHA-256. CMake rejects missing, non-hex, or
non-64-character text and normalizes valid pins to lowercase. Official stable/beta workflows
source the value from the repository Actions variable; the native release job
also compares it with the signer certificate extracted from the signed DLL.
The requirement defaults off so local and fork CI builds remain possible, but
an unpinned Release build cannot load a native core and is not distributable;
environment overrides are limited to Debug builds.

The top-level CMake project enables both C and CXX so Ubuntu system-package
HDF5 discovery can run its C probe while the application code remains C++17.
`cmake/MIBLinkHelpers.cmake` accepts both the namespaced HDF5 targets used by
Conan/Linux packages and the un-namespaced `hdf5-shared` / `hdf5-static`
targets exported by upstream/Homebrew HDF5 2.x. With OpenCV 5 it additionally
links `opencv_geometry`; OpenCV 4 keeps the existing imgproc-only path.

## Commands

```bash
# Configure (once)
cmake --preset windows-default

# Build Debug
cmake --build build --config Debug

# Build Release
cmake --build build --preset windows-default-build-release

# Backend-only (Linux)
cmake --preset linux-backend-only
cmake --build --preset linux-backend-only-build --target mib_backend mib_backend_smoke_test emodulus_lut_catalog_test
ctest --preset linux-backend-only-test -L backend --output-on-failure

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
- `cmake/MIBOptions.cmake` adds `MIB_ENABLE_MINDVISION`:
  - `OFF` by default
  - `ON` enables MindVision SDK discovery on Windows
- `cmake/MIBDependencies.cmake` sets `MIB_HAS_MINDVISION`:
  - `ON` when `WIN32 AND MIB_ENABLE_MINDVISION`
  - `OFF` otherwise
- When `MIB_HAS_EGRABBER=OFF`, build wiring skips:
  - EGrabber include path (`C:/Program Files/Euresys/eGrabber/include`)
  - Coremor include path (`include/Coremor`)
  - Coremor import library (`XMT_DLL_SER.lib`)
  - Windows-only autofocus implementation (`AutofocusService.cpp`)
- Non-Windows uses `src/backend/services/AutofocusService.stub.cpp` so Linux
  cloud builds can compile and run mock/non-hardware workflows.
- When `MIB_HAS_MINDVISION=ON`, CMake requires:
  - `MindVision/CameraApiLoad.h` (or `CameraApiLoad.h`)
  - `MVCAMSDK.dll` or `MVCAMSDK_X64.dll`
  - SDK root overrides via `MIB_MINDVISION_SDK_ROOT` or the
    `MIB_MINDVISION_SDK_DIR` environment variable
- When MindVision is disabled, the backend still compiles a stub camera
  implementation and the connect UI keeps mock/EGrabber workflows intact.

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
