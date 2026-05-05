 # 2026-05-05 — HDF5 exporter build fixes for Linux + macOS

 ## Summary

 The legacy `scripts/` HDF5 exporter packaging flow had platform assumptions that
 made Unix builds fragile:

 - `hdf5_export.spec` depended on cwd behavior and could fail when invoked from
   outside `scripts/`.
 - `build_mac.sh` behaved like a mac-only script and failed on Linux cloud
   environments that lacked `python3-venv` / `ensurepip`.

 This task updates the exporter packaging path so it compiles on both Linux and
 macOS with the same script.

 ## What changed

 ### `scripts/hdf5_export.spec`

 - Made spec directory resolution robust across invocation styles:
   - prefer `__file__` when available
   - fallback to `SPECPATH` when running under PyInstaller spec execution
   - fallback to `Path.cwd()` as last resort
- Removed stale hidden import `numpy.core._methods` which is not present in
  NumPy 2.x and caused noisy build errors.

 ### `scripts/build_mac.sh`

 - Reworked script as Unix build entrypoint (macOS + Linux).
 - Added OS detection (`Darwin` vs `Linux`) and platform-specific output checks:
   - macOS expects `dist/hdf5_export_app.app` (+ optional DMG)
   - Linux expects `dist/hdf5_export_app` ELF executable
 - Added resilient Python environment handling:
   - uses venv when available and complete
   - detects incomplete `.venv` (missing `pip`) and falls back to system Python
   - skips `pip` self-upgrade when using distro-managed/system Python
 - Added dependency install fallback for system Python:
   - try `pip install --user -r requirements.txt`
   - retry with `--break-system-packages` when needed

 ### `docs/howto/hdf5-export-app.md`

 - Added Linux prerequisites and build/run instructions.
 - Updated overview to state Linux packaging support.
 - Unified manual build section for macOS/Linux.

 ## Validation

 Ran in Linux cloud environment:

 1. `bash -n scripts/build_mac.sh`
 2. `bash ./scripts/build_mac.sh --clean`
    - result: success, output `scripts/dist/hdf5_export_app`
 3. `python3 -m PyInstaller scripts/hdf5_export.spec --clean --workpath /tmp/h5build-root --distpath /tmp/h5dist-root`
    - result: success, confirms spec works when invoked from repo root

 Notes from PyInstaller warnings are environment/runtime package issues
 (`libxcb-cursor.so.0`, `libtiff.so.5` on this host), not spec syntax failures.

 ## Files touched

 - `scripts/hdf5_export.spec`
 - `scripts/build_mac.sh`
 - `docs/howto/hdf5-export-app.md`
