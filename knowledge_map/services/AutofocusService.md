# AutofocusService

> Closed-loop autofocus: drives a Coremor piezo nanopositioner over serial
> using ring-ratio feedback from [[ProcessingService]].

**Source:** `src/backend/services/AutofocusService.cpp`,
`src/backend/services/AutofocusService.stub.cpp`,
`include/backend/services/AutofocusService.h`
**Related:** [[ProcessingService]], [[../frontend/NanopositionerTab]],
[[../domain/Glossary]] (ring ratio)

## Responsibility

- Manage serial connection to nanopositioner (`connect`, `disconnect`,
  `probeComPort` for discovery).
- Consume ring-ratio samples via `onRingRatio(ringRatio, timestampNs)`
  (wired by [[../architecture/AppBackend]]).
- Run a control loop on its own thread; manual voltage control
  (`increaseVoltage` / `decreaseVoltage`) too.
- Expose running statistics to the UI: `getAverageRingRatio`,
  `getMedianRingRatio`, `getLastRingRatioUpdateUs`.
- Emit human-readable status via `StatusCallback`.

## Config — `AutofocusService::Config`

- `focusSetpoint`, `focusRange` — target ring-ratio and tolerance
- `voltageStep`, `fineVoltageStep`, `maxVoltage`, `minVoltage`, `initialVoltage`
- `manualVoltageStep`
- `ringRatioStaleMs` — drop samples older than this
- `requireNewSamplePerStep`, `minSamplesPerStep`
- `safeShutdownVoltage` — applied on disconnect
- `focusDirection` — whether increasing voltage increases ring ratio

## Threading

Three threads are involved in the autofocus path:

| Thread | Lifetime | Role |
|---|---|---|
| Caller (ProcessingService realtime) | external | Calls `onRingRatio(ringRatio, ts)` on every valid frame — O(1) push into `pendingSamples_` + atomic freshness markers + `notify_one`. No sort, no deque work, no allocator pressure on the realtime thread. |
| `statsThread_` | constructor → destructor | `statsLoop()` drains `pendingSamples_` under `pendingSamplesMutex_`, writes into the `std::deque<double>` ring-ratio buffer (`ringRatioMutex_`), trims to `MAX_BUFFER_SIZE` (1000), and refreshes `{median, average, min, max}RingRatio_` atomics. Wake-rate is capped at ~100 Hz via a 10 ms min-drain interval so the O(n log n) sort amortises across a batch. |
| `controlThread_` | connect → disconnect | `controlLoop()` at ~20 Hz: reads stats atomics, talks Coremor XMT over serial, applies manual or automatic voltage steps. |

Two mutexes: `pendingSamplesMutex_` (producer ↔ `statsThread_`) and
`ringRatioMutex_` (`statsThread_` ↔ `controlThread_`). The
producer-consumer split means the ProcessingService realtime thread
never touches `ringRatioMutex_` or the sort, so autofocus work can never
delay the [[TriggerService]] CV wake-up.

### Post-step buffer clear

After a voltage step the control loop clears **both** `pendingSamples_`
and `ringRatioBuffer_` (`std::scoped_lock` over both mutexes atomically)
so pre-step samples sitting in the inbox don't leak into post-step
statistics. Same pattern on `disconnect()`.

## Gotchas

- `probeComPort` is **static** and requires the service to be
  **disconnected** — it opens/closes the port itself for discovery.
- `focusDirection` inverts the sign of voltage adjustments — wrong value
  causes runaway.
- Ring-ratio buffer does not track stream ID; if camera restarts, consider
  clearing state.
- `statsThread_` starts in the constructor and stops in the destructor —
  **not** in `connect` / `disconnect`. This is intentional: the UI still
  expects statistics even when the nanopositioner is not connected, and
  the realtime pipeline pushes samples regardless.
- `ringRatioSequence_` and `lastRingRatioUpdateUs_` are updated in
  `onRingRatio` (inline) so the control loop's freshness gate sees data
  arrival immediately even if `statsLoop` is a few milliseconds behind
  on the sort. The median may lag by up to ~10 ms (a single drain
  interval); the 50 ms control-loop tick absorbs this.

## Third-party

See `include/Coremor/` for the XMT_DLL_SER DLL shipped with the repo.

## Platform behavior

- **Windows (`MIB_HAS_EGRABBER=1`)**: full Coremor-backed implementation
  (`AutofocusService.cpp`) is compiled.
- **Non-Windows (`MIB_HAS_EGRABBER=0`)**: `AutofocusService.stub.cpp` is
  compiled instead. It keeps the public API shape but `connect()`/probe
  operations are unsupported and return failure, which allows cloud/Linux
  builds to compile and exercise non-hardware features.
