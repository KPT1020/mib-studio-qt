# Threading Model

> Who runs on which thread. Getting this wrong causes deadlocks, missed
> frames, or UI freezes.

**Related:** [[Data-Flow]], [[AppBackend]], [[../services/CaptureService]],
[[../services/ProcessingService]]

## Threads at a glance

| Thread | Owner | Blocks on | Role |
|---|---|---|---|
| Main / GUI | Qt event loop (`main.cpp`) | Qt events | UI, controllers, timer ticks |
| Capture | [[../services/CaptureService]] `run()` | `camera->grabFrame()` | Acquires frames, pushes to FrameStore, fires callback |
| Processing workers | [[../services/ProcessingService]] pool | job queue | Generic job executor (size = `hardware_concurrency()` by default) |
| Realtime processing | [[../services/ProcessingService]] `realtimeLoop()` | FrameStore writeIndex | Low-latency per-frame analysis; drop-frames mode skips to latest |
| Autofocus stats | [[../services/AutofocusService]] `statsLoop()` | `pendingSamplesCV_` + 10 ms drain interval | Drains ring-ratio samples pushed by `ProcessingService` realtime thread, maintains 1000-sample deque, refreshes `{median,average,min,max}RingRatio_` atomics. Runs for the full lifetime of the service, not just while connected. |
| Autofocus control | [[../services/AutofocusService]] `controlLoop()` | serial COM | Reads ring-ratio stats atomics, writes voltage to nanopositioner. Runs only between `connect()` / `disconnect()`. |
| Trigger | [[../services/TriggerService]] `triggerLoop()` | `triggerCV_` | Issues camera digital-output pulse on target-group events |
| Syringe pump poll | [[../services/SyringePumpService]] per pump | serial (Modbus RTU) | UI-driven status polls |
| Frame-recording | `AppBackend` `frameRecordingThread_` | FrameStore | Only active in recording mode; drains non-empty frames into HDF5 |

## Sync primitives

- `std::atomic<bool>` flags gate the thread loops.
- `FrameStore` internal mutex serialises push/query. See
  [[../data-model/FrameStore]].
- `ProcessingService` uses `std::condition_variable_any` for the worker
  queue; realtime loop polls FrameStore by absolute write-index.
- Qt signals from non-GUI threads go through
  [[../frontend/System-Utilities]] `AppBackend::setBackgroundCaptureCallback` (signal bridge).

## Experiment vs Monitoring vs Realtime

`ProcessingService` has three accumulation modes:

- **Realtime snapshot** — always available when realtime loop is running;
  UI reads latest via `getLatestSnapshot`.
- **Monitoring ring buffers** — fixed 1000-frame ring for live charts in
  [[../frontend/ExperimentMonitoringTab]]; active whenever realtime is on.
- **Experiment accumulation** — bounded vectors collected while
  `experimentActive_ == true`, periodically flushed to HDF5 via
  `flushBufferedFrames`. The realtime thread drops sampled invalid frames first
  if HDF5 falls behind and the backlog reaches its cap.

## Shutdown order

Stopping capture first drains the realtime loop safely. See
`docs/howto/safe-start-stop-egrabber.md` for EGrabber-specific shutdown
requirements (including `StreamModule` stat refresh — see [[../conventions/Code-Conventions]]).
