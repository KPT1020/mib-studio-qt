# 2026-04-20 — Cloud toolchain fix for `-lstdc++` link failure

## Summary

In the Linux cloud environment, CMake configure initially failed before project
checks with:

`/usr/bin/ld: cannot find -lstdc++`

Root cause was a host toolchain mismatch: `/usr/bin/c++` was pinned to
`clang++` via alternatives, and in this image that path resolved linker search
paths where unversioned `libstdc++.so` was not available.

## Resolution

- Switched system `c++` alternative to GNU g++:

```bash
sudo update-alternatives --set c++ /usr/bin/g++
```

- Verified minimal link succeeds:

```bash
printf 'int main(){return 0;}' | c++ -x c++ - -o /tmp/cxx-link-test
```

## Follow-up changes

After fixing the host linker issue, Linux configure still failed in cloud
images where `onnxruntimeConfig.cmake` was unavailable. To keep cloud builds
usable for non-hardware/non-YOLO workflows:

- `CMakeLists.txt` now treats ONNX Runtime as optional on Linux/cloud paths
  (`find_package(onnxruntime CONFIG QUIET)`).
- Added `MIB_HAS_ONNXRUNTIME` compile definition.
- Added `src/backend/services/YoloService.stub.cpp`, wired when
  `onnxruntime::onnxruntime` target is absent.
- Existing `YoloService.cpp` remains active when ONNX Runtime is present.

## Outcome

- The original host linker error (`cannot find -lstdc++`) was resolved.
- Linux CMake configure now succeeds even when ONNX Runtime is not installed.
- `cmake --build build-linux` succeeds for `mib_backend`, `capture_processing_test`,
  and `mib_studio_qt` with the YOLO stub path.
- `ctest` currently fails due to an invalid mock frame payload (`CRC error` in
  `data/mock_frames/frame_000.png`), unrelated to toolchain/ONNX availability.
- On this cloud image, `onnxruntime` CMake package metadata was unavailable
  via apt and conflicted in Conan's Linux graph with the current Qt/OpenCV
  stack. Build wiring was updated to keep ONNX optional on Linux/non-provisioned
  environments by compiling `YoloService.stub.cpp` when `onnxruntime` target
  is not found.

## Notes for future cloud runs

- If this exact linker error appears, fix toolchain alternatives first before
  investigating project code or CMake logic.
- Distinguish host toolchain failures from project dependency failures:
  - host failure: cannot link trivial C++ program
  - project failure: package discovery or dependency graph conflicts
