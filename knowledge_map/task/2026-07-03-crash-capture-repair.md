# 2026-07-03 — Crash-capture pipeline repair + bug-scan batch

Full-codebase scan for crash-inducing bugs plus an end-to-end audit of the
logging and Sentry pipelines ("do they actually catch everything?").
Companion entry in [[../current-state/Recent-Work]].

## Why the pipeline was broken

`SetUnhandledExceptionFilter` and `std::signal` REPLACE the installed
handler — there is no chaining. `CrashReporter::init` called them *after*
`sentry_init`, which is where Crashpad (Windows) / the inproc backend
(Linux) install theirs. Net effect: with a DSN configured, a real crash
wrote a local `.dmp` and went to WER; Sentry got nothing at crash time,
and the next launch sent only a "Recovered crash dump" *message* (no
stack, dump never uploaded) while renaming the file `.uploaded`.

## What changed (all in one commit)

- **CrashReporter** (`src/backend/services/CrashReporter.cpp`): Sentry
  keeps its handlers; `on_crash` hook writes the local sidecars; local
  SEH/signal handlers only when Sentry is not live; recovered dumps go
  through `sentry_capture_minidump`; `-crash` dumps retire without
  re-capture (Crashpad already reported them); event-scope pollution from
  `sentry_set_extra` fixed with `sentry_remove_extra`; terminate handler
  records the uncaught exception's `what()` (`-terminate.txt`) and
  flushes; Qt-fatal and `captureException` flush; `Config::handlerPath`
  pins `crashpad_handler.exe`. Details in [[../services/CrashReporter]].
- **Logging** (`Logger.cpp`, `main.cpp`, `AppBackend.cpp`): file logger
  up before CrashReporter; idempotent init; Program-Files fallback moved
  into `Logger::initFromDataDir`; temp-dir fallback file sink;
  `rotate_on_open=false`. Details in [[../conventions/Logging]].
- **main.cpp**: `CrashReporter::shutdown()` (flush) on all exit paths;
  backend-init failure sends a Sentry message; last `std::cout` violation
  → `SPDLOG_INFO`.
- **Worker-thread guards**: `TriggerService::triggerLoop`,
  `AutofocusService::{stats,control}Loop`, `AppBackend` frame-recording
  thread — try/catch + `captureException` (an escaping exception was
  `std::terminate` with no report).
- **Bug-scan fixes**: HDF5 rank guards (7 sites) + create-path geometry
  guards; MindVision `grabFrame` copies under `stateMutex_` (UAF vs
  `stop()`); torn-`std::function` fixes (AutofocusService `notifyStatus`,
  AppBackend fatal-save-error mutex + capture to Sentry);
  NanopositionerTab marshals status updates to the GUI thread and clears
  the callback in its dtor; MainWindow clears the fatal-save-error
  callback in its dtor; ExperimentMonitoringTab clamps overlay ROI crops;
  recording thread resyncs after `FrameStore::resize`; `release.yml`
  passes `-DMIB_SENTRY_ENVIRONMENT`.
- **Tests** (regression-first, each proven failing against the pre-fix
  code): `backend.crash_reporter_terminate`, `backend.logger_init_fallback`,
  `recording.hdf5_foreign_rank`.

## Verified findings intentionally NOT fixed here (tracked debt)

| Finding | Where | Severity | Why deferred |
|---|---|---|---|
| `realtimeBatchLoop` captures stack-local atomics by ref into batch callbacks; an in-loop exception can leave workers writing a dead stack frame | `ProcessingService.cpp` (~1848, 2033) | crash-possible | Needs a lifetime redesign (heap/shared state or scope-guard join), not a spot patch |
| Whole-file HDF5 load into RAM (default-on "Regenerate entire file") | `BatchMaskDialog.cpp` onRun/loadHdf5Inputs | crash-likely on multi-GB files | Needs chunked processing |
| Experiment stop path blocks the GUI thread (flush/drain/close) | `MainWindow::onStopExperiment` | ui-hang | Known stop-lag work, own exec-plan |
| Fully synchronous review-tab exports / buffer save | `HdfReviewTab`, `BufferSaveDialog` | ui-hang | Needs worker + progress UI |
| Nested `QEventLoop::exec()` without `ExcludeUserInputEvents` in profile downloads | `ProfileManager::downloadUrlBlocking` | ui-hang / re-entrancy | Needs async rework of 3 call sites |
| `CameraGetImageBuffer`/`CameraUnInit` handle race (SDK validates handles, so low blast radius) | `MindVisionCamera.cpp` grab vs stop | low | SDK-level; needs handle refcounting |
| `SPDLOG_ACTIVE_LEVEL` set only for Debug config — release builds compile out DEBUG/TRACE entirely | `src/backend/CMakeLists.txt` | field-diagnostics gap | Product decision (binary size / hot-path cost vs field debugging) |
| QtConcurrent `.result()` rethrows worker exceptions unguarded in finished-slots | `DeviceInitManager.cpp:111`, `MainWindow.cpp:1045` | crash-possible | Guard when touching device-init next |

## Follow-ups worth considering

- Sentry enablement is contingent on the `SENTRY_DSN` repo secret; if it
  is unset, every shipped installer runs local-only with just a yellow CI
  log line as signal. Consider failing the release job loudly instead.
- `EGrabberCamera.cpp:69` swallows the vendor-name exception without a
  log line (intentional but silent).
