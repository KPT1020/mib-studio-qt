# AGENTS.md

## Cursor Cloud specific instructions

### Scope: Linux backend-only

Cloud VMs are configured for **backend-only** development (`MIB_BUILD_BACKEND_ONLY=ON`). This builds `mib_backend` (static library) and `mib_backend_smoke_test`; it does **not** build `mib_studio_qt` (Qt Widgets/Charts GUI requires Windows or a full Linux preset with extra packages).

### System packages (one-time / image)

Ubuntu packages required for `linux-backend-only` (see also `docs/howto/linux-build.md`):

```bash
sudo apt install cmake build-essential pkg-config git \
  qt6-base-dev qt6-serialport-dev \
  libopencv-dev libhdf5-dev libspdlog-dev nlohmann-json3-dev libsqlite3-dev
```

**Compiler:** CMake must use GCC, not Clang. If configure picks Clang (`/usr/bin/clang++` via `c++` alternative), set:

```bash
sudo update-alternatives --set c++ /usr/bin/g++
```

### Configure, build, test

From repo root:

```bash
cmake --preset linux-backend-only
cmake --build --preset linux-backend-only-build --target mib_backend mib_backend_smoke_test
ctest --preset linux-backend-only-test -L backend --output-on-failure
```

Build output: `build/linux-backend/`. Smoke test binary: `build/linux-backend/mib_backend_smoke_test`.

### What the smoke test exercises

`mib_backend_smoke_test` / CTest label `backend` runs **Hdf5Service** only (temp HDF5 open, dataset init, flush, reload, close). No camera, no Qt GUI, no hardware.

### Full app on Linux (out of backend-only scope)

For `mib_studio_qt` with mock camera, use `linux-system-release` and install `qt6-charts-dev` plus Ninja (`docs/howto/linux-build.md`). Mock capture uses `MIB_CAMERA_MODE=mock` and `MIB_MOCK_CAMERA_DIR` (sample frames under `data/mock_frames/`).

### Gotchas

- **ONNX Runtime** is optional on Linux; YoloService builds as a stub when `onnxruntime::onnxruntime` is missing (CMake warning is expected).
- **EGrabber / Coremor** are Windows-only; Linux uses `MockCamera` and stub services when `MIB_HAS_EGRABBER=0`.
- Preset ignores conda/homebrew prefix paths via `CMAKE_IGNORE_PREFIX_PATH` so system Qt/OpenCV are preferred.
