# 2026-08-13 — Native macOS HDF5 exporter artifacts

## Summary

The tools documentation advertised `hdf5_export_app.app`, but the maintained
PyInstaller spec only emitted a standalone executable. The macOS build script
did not fail when the expected bundle or DMG was absent, and there was no
hosted macOS build producing a downloadable artifact.

## Changes

- `tools/hdf5_export_app/hdf5_export.spec` now resolves its source paths from
  the spec location and defines a native onedir `BUNDLE` on macOS while
  preserving one-file Windows/Linux output.
- `tools/build_mac.sh` now validates that it is running on macOS, supports an
  exporter-only build, verifies the app's ad-hoc signature, and emits an
  architecture-labelled DMG plus SHA-256 checksum.
- `.github/workflows/macos-hdf5-exporter.yml` builds native Apple Silicon and
  Intel variants, validates architecture/signing/plist structure, launches the
  packaged GUI as a smoke test, and retains both DMGs as workflow artifacts.
- `scripts/test_hdf5_exporter_spec.py` executes the spec with stub PyInstaller
  targets to guard the macOS bundle and non-macOS one-file branches.

## Distribution boundary

The artifacts are ad-hoc signed by PyInstaller and are intended for internal
use. They are not Apple notarized, so first launch requires Control-click >
Open. A warning-free public distribution still requires Developer ID secrets
and a notarization step.

## Verification

- `python3 scripts/test_hdf5_exporter_spec.py -v`
- `bash -n tools/build_mac.sh`
- `python3 scripts/check_docs.py`
- GitHub Actions matrix: `macos-15` (`arm64`) and `macos-15-intel`
  (`x86_64`), including packaged-GUI startup smoke tests.
