# StartupProbe

> Startup lifeline that converts silent launch failures into an actionable
> message. `main()` writes a stage-by-stage marker file while the app boots
> and removes it once the main window is visible; a stale marker found on
> the next launch means the previous attempt died without ever showing UI,
> and the app tells the user which stage it died at and where the crash
> artifacts live.

**Source:** `src/backend/diagnostics/StartupProbe.cpp`,
`include/backend/diagnostics/StartupProbe.h`
**Wired in:** `src/frontend/core/main.cpp`
**Related:** [[../services/CrashReporter]], [[CrashStateMirror]]
**Test:** `backend.startup_probe` (`tests/backend/startup_probe_test.cpp`)

## Why

The crash handler ([[../services/CrashReporter]]) intentionally suppresses
the Windows error dialog: a crash during startup writes a `.dmp` + `.json`
and the process simply exits. From the operator's point of view the app
"doesn't open, silently, with no error" — and nothing on screen points them
at the artifacts. The probe closes that loop from *inside* the app, needing
no working logger, backend, or network.

## Mechanism

- Marker file `startup.inprogress` in the diagnostics dir
  (`%LOCALAPPDATA%\MIB_Studio_Qt` on Windows; `{exeDir}/data` fallback —
  same parent as `crashes/` and `crash_log.txt`).
- `begin()` consumes any stale marker and reports `{stage, detail}` for it;
  `stage(name)` overwrites the marker as boot progresses; `complete()`
  deletes it once `MainWindow::show()` has run.
- Stages recorded by `main()`: `begin` → `settings-migration` →
  `crash-reporter-install` → `backend-init` → `main-window-create` →
  (marker removed).
- Each stage is also set as the Sentry tag `startup_stage`, so remote
  crash reports show how far boot got even without the local marker.
- A marker whose recorded pid is still alive is a concurrently starting
  second instance, not a failed launch, and is not reported.

## Gotchas

- Qt-free by design (std::filesystem only): it must run before
  `QApplication`-dependent services and stays testable in the
  `linux-backend-only` lane.
- `begin()` never throws — any filesystem failure degrades the probe to
  inactive so diagnostics can never break boot.
- Windows pid recycling can rarely mask a genuine failed launch as a "live
  instance" (never the reverse — no false alarms for concurrent starts).
- Crashes *after* the window is shown leave no marker on purpose: those are
  in-session failures with their own crash artifacts, not silent launches.

**Up**: [[_MOC|Diagnostics MOC]]
