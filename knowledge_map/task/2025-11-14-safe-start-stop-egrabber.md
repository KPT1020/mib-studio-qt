# Task: Safe start/stop for EGrabber and resolve -1012 during shutdown

Date: 2025-11-14

## Context

Intermittent GenTL error `-1012` (“An operation has been aborted before it could be completed”) appeared in logs on stop/restart at high FPS (~5 kHz). This aligns with Euresys SDK behavior: blocking `pop()/processEvent()` will raise ABORT when shutting down the stream.

## Changes

- Implemented SDK-recommended shutdown sequence in `src/camera/common/EGrabberCamera.cpp`:
  - `grabber_->stop()` with `gentl_error` handling that ignores `gc::GC_ERR_ABORT` (debug log).
  - `grabber_->cancelPop()` to wake any thread blocked in `pop()`.
  - `grabber_->reallocBuffers(0)` then reset handles.
- Treated `GC_ERR_ABORT` as expected in acquisition path:
  - `grabFrame()` and `replenishPendingFrames()` catch `gentl_error`; ABORT returns gracefully with debug log instead of error/stop.
- Documentation added: `docs/howto/safe-start-stop-egrabber.md` with sample references.

## Rationale

- Mirrors official samples:
  - Wake blocked `pop()` and ignore ABORT during stop (`displayLatestBuffer.cpp`).
  - Events examples stop/join threads before `grabber.stop()` and disable events afterward.
  - Start pattern from `310-high-frame-rate.cpp` retained.

## Files touched

- Updated: `src/camera/common/EGrabberCamera.cpp`
- Added doc: `docs/howto/safe-start-stop-egrabber.md`

## Expected impact

- No spurious error logs on normal shutdown.
- Prevents deadlocks/hangs by waking `pop()` with `cancelPop()`.
- Safer restart robustness at high frame rates.

## References

- `egrabber-sample-programs/cpp/display-latest-buffer/displayLatestBuffer.cpp`
- `egrabber-sample-programs/python/300-events-mt.py`
- `egrabber-sample-programs/cpp/egrabber-snippets/samples/310-high-frame-rate.cpp`


