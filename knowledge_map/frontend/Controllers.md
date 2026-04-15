# Controllers

> Thin `QObject`-based controllers that translate UI actions into
> [[../architecture/AppBackend]] calls and emit Qt signals for the UI.

**Source:** `src/frontend/controllers/`, `include/frontend/controllers/`

## CameraController

`CameraController(AppBackend& backend)` — wraps capture start/stop.

- `isConfigured()`, `isRunning()`
- `startCapture(QString* errorMsg)` — invoked by [[MainWindow]]
- `stopCapture(bool force, QString* errorMsg)`
- Signals: `cameraStarted`, `cameraStopped`, `error(QString)`

## ExperimentController

`ExperimentController(AppBackend& backend)` — owns the experiment state
machine and the HDF5 file path for the current run.

- `enum class State { Idle, Starting, Active, Stopping }`
- `startExperiment(hdf5FilePath, errorMsg)`,
  `stopExperiment(errorMsg)`
- `startTimeNs()`, `endTimeNs()` accessors for metadata
- Signals: `stateChanged(State)`,
  `experimentStarted(startTimeNs)`,
  `experimentStopped(endTimeNs, validFrames, invalidFrames)`,
  `error(QString)`

## Related

- [[MainWindow]] owns both controllers.
- [[../services/ProcessingService]] reacts to
  `startExperiment`/`endExperiment` — the controller calls into the backend.
- HDF5 flushes at stop-time are coordinated through
  [[../services/Hdf5Service]] and `ProcessingService::flushBufferedFrames`.
