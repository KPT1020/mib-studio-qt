# RecorderService

> Legacy raw frame container writer kept for source compatibility.

**Source:** `src/backend/recording/RecorderService.cpp`,
`include/backend/recording/RecorderService.h`

## Responsibility

- `openForWrite(containerDir)` — currently returns `false`; the legacy raw
  writer is unavailable.
- `writeFrame(data, size, pitch, w, h, pixelFormat, partCount, partSize,
  timestampNs, userData)` — currently returns `false`.
- `close()`, `isOpen()`.

Internals are hidden behind a PIMPL (`struct Impl`).

## Threading

Single-writer expected if this compatibility surface is re-enabled later. It is
not used by the common `AppBackend` flow; HDF5 is the primary sink.

## Gotchas

- The Record button and main frame-recording flow use
  `AppBackend::startFrameRecording`, which writes to HDF5 through
  `Hdf5Service`. The disabled legacy raw writer does not affect that main
  function — see [[../architecture/AppBackend]].
