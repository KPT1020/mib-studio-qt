# ExperimentCoordinator

> Backend-owned **experiment readiness transaction** and **immutable run
> snapshot** (issue #369; host-SDK portion of #274). The only thing that can
> authorize an experiment Start is a readiness evaluation performed by the
> backend against its *actual* current state — and that authorization is
> generation-tagged, so a stale preflight never starts a run.

**Source:** `src/backend/app/ExperimentCoordinator.cpp`,
`include/backend/app/ExperimentCoordinator.h`,
`include/backend/app/ExperimentReadiness.h` (Qt-free types + JSON serializers)
**Tests:** `tests/backend/experiment_readiness_test.cpp`
(`backend.experiment_readiness`, normal + TSan)
**Related:** [[AppBackend]], [[../services/CaptureService]],
[[../services/ProcessingService]], [[../services/Hdf5Service]],
[[../data-model/HDF5-Storage]], [[../frontend/MainWindow]]

## Responsibility

- `evaluateReadiness(outputPath, profileId)` returns an
  `ExperimentReadinessSnapshot`: a list of `ReadinessGate`s (`id`, `status`,
  `reason`, `remediation`, `detail`), `ready` (no gate blocks), a
  **generation**, and the `candidate` `RunConfigurationSnapshot` the
  evaluation was based on. `GateStatus` is `Pass / Warn / Fail / Unavailable /
  NotRequired`; `Fail` and `Unavailable` block — **unknown is never Pass**.
- The generation increments only when an *invalidation input* changes
  (`InvalidationKey`: capture generation + readiness, effective camera source
  + fallback flag, active delivery mode, processing config version, raw
  `config.json` sha, core version/sha/pin, background generation, ROI,
  pixel-to-micron factor, output path, profile id, unresolved fault). A stable
  state keeps its generation, so a preflight stays usable until something
  actually changes.
- `start(ExperimentStartRequest{outputPath, readinessGeneration, profileId,
  acknowledgeLatestFrameDrops})` is the serialized Start transaction:
  1. `try_lock` — a concurrent transaction gets `Busy`; a run in progress
     gets `AlreadyActive`.
  2. Re-evaluate now; `readinessGeneration` mismatch → `StaleReadiness`
     (reconnect, config/background/core/ROI/output change, new fault since
     the presented preflight). Not ready → `NotReady`. LatestFrame without
     acknowledgement → `NotReady`.
  3. Freeze the `RunConfigurationSnapshot` from the evaluated candidate
     (start generation, host + wall-clock start time, output path).
  4. Open the HDF5 file + `initializeDatasets()` (`StorageFailed` rolls back
     to Idle), then persist the snapshot **first** via
     `Hdf5Service::writeRunSnapshotJson` (`ProvenanceFailed` closes and
     removes the file). A run without its frozen snapshot is never Running.
  5. `setExperimentAccountingContext(captureGeneration, latestFrame)` +
     `ProcessingService::startExperiment()`; publish `Running`.
- `finish()` releases the frozen snapshot and returns to `Idle`; the caller
  ([[../frontend/MainWindow]] stop path) still owns flush / metadata /
  `closeFile()`.
- `reportUnresolvedFault(code, message)` / `clearUnresolvedFault()`: a save
  or provenance failure from the last run blocks the next Start
  (`lifecycle.fault` gate) until the operator acknowledges it.

## Gates

| id | Fail / Unavailable when | Warn when |
|---|---|---|
| `camera.session` | capture not `Running` + `cameraReady` (reason includes the session's `lastFailure`) | — |
| `camera.source` | requested hardware fell back (`CameraSourceInfo.fallback`) or source unknown | explicit mock camera |
| `camera.deliveryMode` | not confirmed by a running backend | `latestFrame` (intentional drops) |
| `camera.geometry` | no frame received yet | — |
| `processing.roi` | — | no ROI (full frame) |
| `processing.core` | pinned core not active | — |
| `calibration.pixelToMicron` | factor not positive | — |
| `processing.background` | — | no background image |
| `trigger.output` | sorting enabled but TriggerService not bound to the running session (`NotRequired` when sorting is off) | — |
| `storage.output` | no path (`Unavailable`), unwritable parent, path is a directory, < 64 MiB free | — |
| `storage.hdf5` / `lifecycle.recording` / `lifecycle.experiment` | file already open / raw recording active / coordinator not Idle | — |
| `lifecycle.fault` | unresolved fault reported | — |
| `telemetry.transportLoss` | no active session | backend cannot / has not reported transport loss |

## RunConfigurationSnapshot (schema v1)

Frozen at Start and never mutated: readiness/start/capture generations,
start times, `CameraSourceInfo` (requested vs effective, simulated,
fallback + reason), delivery modes, `TimestampDescriptor` text, ROI, frame
geometry, processing-core identity + pin state, processing config version +
canonical sha, raw `config.json` sha, profile id, pixel-to-micron factor,
background presence/generation/sha, trigger requirement/binding, output
path, realtime mode, application version/build/OS. `runSnapshotToJson()` /
`readinessToJson()` produce the stable-key JSON stored on `/run_provenance`
(see [[../data-model/HDF5-Storage]]).

## Threading

`mutex_` serializes evaluate/start/finish/fault calls; evaluation only reads
service snapshots (`lifecycleSnapshot()`, `telemetrySnapshot()`,
`getProcessingConfig()`, …) and never takes a service lock while holding one
of its own for long. Safe to call from a UI timer. `start()` holds the mutex
across the HDF5 open + provenance write, which is why a second caller gets
`Busy` instead of racing.

## Gotchas

- The preflight must be evaluated with the **same** `outputPath` and
  `profileId` the Start presents — both are invalidation inputs.
- `camera.source` fails on a *fallback* (hardware requested, mock built) and
  only warns on an *explicit* mock selection; [[AppBackend]] records the
  distinction (`cameraSourceInfo()`), never silently.
- The coordinator does not close the HDF5 file on `finish()`; the stop path
  writes experiment info / accounting / provenance and closes it.
