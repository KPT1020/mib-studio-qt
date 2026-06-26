# 2026-06-11: Remote-managed Young's modulus LUT

## Context

KIN-54 asked for the Young's modulus LUT to behave like the profile catalog:
public R2 manifest, SHA-256 verification, user-writable cache, and bundled
fallback.

## What changed

- Added `EModulusLutCatalog` to resolve the active LUT, fetch the manifest, and
  cache/download the LUT into the app-local data tree.
- Updated `AppBackend` to prefer the managed LUT path and log source,
  revision, checksum status, and fallback state at startup.
- Added `publish-emodulus-lut.py` and `verify-emodulus-lut-manifest.py` for
  R2 publishing/verification.
- Added `tests/backend/emodulus_lut_catalog_test.cpp` to cover the remote
  update path plus local fallback.

## Validation

- `cmake --preset linux-backend-only`
- `cmake --build --preset linux-backend-only-build`
- `ctest --preset linux-backend-only-test --output-on-failure`
- `python3 scripts/check_docs.py`

## Notes

- Backend-only builds now link `QtNetwork` because the LUT manifest fetch path
  runs inside `AppBackend` during startup.
- The helper supports `file://` URLs and a cache-dir override for offline
  tests, but the production default remains the public R2 manifest URL.
