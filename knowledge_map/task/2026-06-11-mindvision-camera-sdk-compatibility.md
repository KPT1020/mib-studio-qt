# 2026-06-11 - MindVision camera SDK compatibility

## Context

Recovered MindVision camera support from the historical implementation branch
and adapted it to the current camera backend / Connect tab / deployment flow.

## Scope

- Added a separate MindVision camera backend implementation under
  `src/backend/camera/mindvision/`.
- Expanded camera discovery and selection to handle EGrabber, MindVision, and
  mock camera workflows independently.
- Added CMake support for opt-in MindVision SDK detection and Windows runtime
  deployment.
- Updated backend/frontend docs and vault navigation notes.

## Validation

- `cmake --preset linux-backend-only`
- `cmake --build --preset linux-backend-only-build`
- `ctest --preset linux-backend-only-test -L backend --output-on-failure`
- `python3 scripts/check_docs.py`

## Notes

- Default Linux/cloud builds keep `MIB_ENABLE_MINDVISION=OFF` and continue to
  work without proprietary SDK files.
- The historical branch used as the source of truth was
  `origin/claude/sync-with-main-8mwTD`.
