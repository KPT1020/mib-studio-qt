# PlaybackService

> Thin UI-facing wrapper around [[../data-model/FrameStore]]. No thread of
> its own.

**Source:** `src/backend/services/PlaybackService.cpp`,
`include/backend/services/PlaybackService.h`
**Related:** [[../data-model/FrameStore]], [[../frontend/System-Utilities]]
(PlaybackPanel)

## Responsibility

- Forward reads to the shared `FrameStore` (`fetchLatest`, `fetchByIndex`,
  `queryRange`).
- Expose convenience: `totalWritten`, `capacity`,
  `getAvailableRange`, `getAvailableTimestampRange`,
  `estimateMemoryBytesForCapacity`.
- Save-to-disk helpers (TIFF export) by index range or timestamp range,
  with optional `filterFn` to skip empty frames.
- `resize(newCapacity)` — delegates to FrameStore; preserves frames if the
  new capacity is big enough.
- `play()` / `pause()` — transport-style flag, read by UI.

## Threading

Synchronous passthroughs. Thread-safety comes from FrameStore's internal
mutex.

## Gotchas

- `fetchByIndex` takes an **absolute** write index (monotonic since start),
  not a slot index. Use `queryRange` to get the current window.
- Save helpers write as TIFF — see `FrameStore::saveFramesToDisk` for the
  single-frame implementation.
