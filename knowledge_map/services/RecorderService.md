# RecorderService

> Raw frame container writer — alternative to HDF5 for recording mode.

**Source:** `src/backend/services/RecorderService.cpp`,
`include/backend/services/RecorderService.h`

## Responsibility

- `openForWrite(containerDir)` — start a new container directory.
- `writeFrame(data, size, pitch, w, h, pixelFormat, partCount, partSize,
  timestampNs, userData)` — append a frame as-is (no processing).
- `close()`, `isOpen()`.

Internals are hidden behind a PIMPL (`struct Impl`).

## Threading

Single-writer expected. Not used directly by the common `AppBackend` flow;
HDF5 is the primary sink.

## Gotchas

- For most use cases, prefer `AppBackend::startFrameRecording` which writes
  to HDF5 — see [[../architecture/AppBackend]].
