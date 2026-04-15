# AutofocusService

> Closed-loop autofocus: drives a Coremor piezo nanopositioner over serial
> using ring-ratio feedback from [[ProcessingService]].

**Source:** `src/backend/services/AutofocusService.cpp`,
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

- `controlThread_` runs `controlLoop()` when enabled.
- Serial writes happen on the control thread (Coremor XMT protocol).
- Ring-ratio buffer (`std::deque<double>`, max 1000) is mutex-protected.

## Gotchas

- `probeComPort` is **static** and requires the service to be
  **disconnected** — it opens/closes the port itself for discovery.
- `focusDirection` inverts the sign of voltage adjustments — wrong value
  causes runaway.
- Ring-ratio buffer does not track stream ID; if camera restarts, consider
  clearing state.

## Third-party

See `include/Coremor/` for the XMT_DLL_SER DLL shipped with the repo.
