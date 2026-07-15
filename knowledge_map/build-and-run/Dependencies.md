# Dependencies

> Third-party stack. Managed by Conan (`conanfile.txt`).

| Package | Version | Shared? | Notes |
|---|---|---|---|
| qt | 6.7.3 | ✓ | **`mib_backend` links no Qt** (epic #246, all clusters done). Frontend adds Core + Gui + Network + Widgets + Charts + Concurrent + ImageFormats. The `linux-backend-only` build still `find_package`s `Qt6::Core` only because 7 `frontend;utility` tests link it directly — de-Qt-ing those is the last step to a truly Qt-SDK-free backend build. |
| spdlog | 1.17.0 | | Logging ([[../conventions/Logging]]) |
| sqlite3 | 3.51.0 | | [[../services/SqliteService]] |
| hdf5 | 1.14.6 | ✓ | C++ API enabled — [[../services/Hdf5Service]] |
| opencv | 4.12.0 | ✓ | dnn=False, openexr=False. Linked modules: `core`, `imgproc`, `imgcodecs`, `videoio` (AVI read/write by [[../data-model/FrameStore]] and [[../services/BatchMaskSources]]) — [[../services/ProcessingService]] |
| onnxruntime | 1.18.1 | | Optional in Linux cloud builds; when unavailable the build uses `YoloService.stub.cpp` and disables YOLO runtime features while keeping the rest of the app buildable — [[../services/YoloService]] |
| nlohmann_json | 3.11.3 | | Config parsing / serialization |
| openssl (libcrypto) | system | ✓ | Linux desktop builds only (`find_package(OpenSSL REQUIRED)` under `UNIX AND NOT APPLE`): Ed25519 detached-signature verification for native processing cores. Optional for the Qt-less wheel configure, whose verifier then fails closed. |

## Vendored / checked-in

- **Euresys EGrabber SDK** — `egrabber-sample-programs/` (reference source
  tree); actual SDK is assumed installed system-side. See
  `docs/integration/egrabber.md`.
- **MindVision SDK** — not vendored. Configure with
  `MIB_ENABLE_MINDVISION=ON` on Windows and point CMake at the SDK root via
  `MIB_MINDVISION_SDK_ROOT` or `MIB_MINDVISION_SDK_DIR`. The build expects
  the MindVision include tree plus `MVCAMSDK.dll` or `MVCAMSDK_X64.dll`.
- **Coremor XMT DLL** — `include/Coremor/` (`.h`, `.lib`, `.dll`). Used by
  [[../services/AutofocusService]].

## How they're wired

- `CMakeLists.txt` calls `find_package(Qt6 ...)` and so on; Conan
  generates the toolchain (`build/conan_toolchain.cmake`).
- `mib_backend` links **no Qt** and has `AUTOMOC OFF` (epic #246): `Gui`,
  `Network`, `SerialPort` were all removed (OpenCV mock-camera decode;
  [[../services/ISerialPort]]; injected LUT HTTP seam, ADR 0002), and `Core`
  went with the crash-reporter handler moving to the frontend. The
  `MIB_BUILD_BACKEND_ONLY=ON` build still `find_package`s `Qt6::Core` **only**
  for the 7 `frontend;utility` tests that link it directly; making those Qt-free
  is the last step before backend-only needs no Qt SDK at all.
- Qt shared DLLs + plugins are deployed next to the exe via
  `windeployqt.exe` in a post-build step.
- Full frontend test builds use Qt Widgets in offscreen mode for the
  `processing_core_dialog_test`; backend-only builds do not generate that
  target.
- OpenCV and HDF5 DLLs are also copied next to the exe (see
  `docs/howto/windows-deploy.md`).
- Windows-only hardware SDK linkage is gated by `MIB_HAS_EGRABBER`
  (`WIN32` => `ON`, otherwise `OFF`):
  - EGrabber headers/system path are only added when `MIB_HAS_EGRABBER=1`.
  - Coremor include/lib wiring is only added when `MIB_HAS_EGRABBER=1`.
- MindVision SDK linkage is gated separately by `MIB_ENABLE_MINDVISION` /
  `MIB_HAS_MINDVISION`:
  - CMake locates the MindVision SDK include tree and runtime DLL only when
    `MIB_ENABLE_MINDVISION=ON` on Windows.
- Default builds keep MindVision disabled so Linux/cloud CI does not require
  proprietary SDK files.
- `QtNetwork` is linked by the backend library so `AppBackend` can manage the
  Young's modulus LUT manifest/cache directly during startup.

## Qt decoupling in progress (epic #246)

`mib_backend` is now **fully Qt-free** — the migration to React + Tauri (ADR
`docs/decisions/0001-react-tauri-migration.md`) removed Qt from the backend
cluster by cluster: `ModbusRtu.h` frames are `std::vector<uint8_t>` (was
`QByteArray`); `MindVisionConfig.h` parses with `nlohmann_json` (was Qt JSON);
the mock camera decodes via OpenCV `cv::imread` (was `QImage` → `Qt6::Gui`
dropped); the syringe pump uses [[../services/ISerialPort]] (was `QSerialPort` →
`Qt6::SerialPort` dropped); the E-modulus LUT catalog delegates HTTP to a
shell-injected seam (ADR 0002 → `Qt6::Network` dropped); and the crash-reporter
Qt log handler moved to the frontend `[[../frontend/System-Utilities]]`
(`QtLogBridge`) so the last `QString`/`qtMessageHandler` usage left the backend
→ `Qt6::Core` dropped and `AUTOMOC` turned off. Remaining before a Qt-SDK-free
backend-only build: de-Qt the 7 `frontend;utility` tests. Tracked in
`docs/exec-plans/active/2026-07-15-qt-decoupling-and-tauri-migration.md`. Only
after those land will `MIB_BUILD_BACKEND_ONLY` build with no Qt SDK.

## Linux cloud build notes

- If CMake fails before project checks with
  `/usr/bin/ld: cannot find -lstdc++`, verify which compiler `/usr/bin/c++`
  resolves to. Some images pin `c++` to a clang alternative while missing the
  matching unversioned `libstdc++.so` path for that toolchain.
- In those environments, switching `c++` to `g++` resolves the runtime linker
  path issue:
  - `sudo update-alternatives --set c++ /usr/bin/g++`

## Related

- [[Build]] for presets and commands.
- `docs/howto/runtime-deploy.md` for runtime deployment details.
