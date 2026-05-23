# 2026-05-22 — Crash monitoring & remote logging

Branch: `claude/crash-monitoring-logging-jUziR`

## Problem

The app experiences random crashes in production. The only protection
before this change was a top-level `try/catch` around
`QApplication::exec()` (`src/frontend/core/main.cpp:124-156`), which:

- Only catches main-thread C++ exceptions.
- Misses SEH access violations, stack overflows, heap corruption,
  signals (SIGSEGV/SIGABRT/SIGFPE), and exceptions thrown on worker
  threads.
- Produces no stack trace, no service-state context, no remote
  notification.
- Release builds don't preserve PDBs, so even the `crash_log.txt`
  fragments we did get were impossible to symbolicate.

## Solution

Two-channel crash pipeline, both wired in early during `main()` before
`QApplication` exists:

1. **[[../services/CrashReporter]]** — installs:
   - `SetUnhandledExceptionFilter` → `MiniDumpWriteDump` (Windows, via
     `dbghelp.lib`).
   - `std::signal(SIGSEGV/SIGABRT/SIGFPE/SIGILL, …)` → writes a `.json`
     state snapshot, then re-raises with default disposition.
   - `std::set_terminate` for uncaught C++ exceptions.
   - `qInstallMessageHandler` to route Qt warnings into spdlog and
     forward Qt fatals as Sentry events.
   - Optional `sentry-native` (Crashpad backend on Windows). When a DSN
     is provided via `MIB_SENTRY_DSN`, the SDK takes ownership of the
     dump-and-upload pipeline; otherwise we fall back to writing
     `.dmp + .json` pairs into `%LOCALAPPDATA%/MIB_Studio_Qt/crashes/`.

2. **[[../diagnostics/CrashStateMirror]]** — a process-global lock-free
   struct with one sub-slot per service area. Services write atomic
   updates at the same call sites where they already log lifecycle
   events. The crash handler reads atomically from this mirror without
   taking any locks the crashing thread might hold.

Both pieces are wired into `main()` (install) and `AppBackend`
(register snapshot fn + seed initial tags), and the relevant services
each got a small set of additive writes — see the "Wiring pattern"
table in [[../diagnostics/CrashStateMirror]].

## Out of scope (intentional)

- Wrapping `ProcessingService::workerLoop` and other worker loops in
  `try/catch`. C++ exceptions on workers will still kill that worker
  silently; this can be added later if the field telemetry shows it
  matters. SEH/native faults from any thread are still caught.
- Symbol-upload automation in CI. PDBs are now produced; uploading via
  `sentry-cli upload-dif` is documented but not yet wired into the
  release workflow.

## Verification plan

1. Build Release; confirm `mib_studio_qt.exe`,
   `mib_studio_qt.pdb`, and (with `MIB_USE_SENTRY=ON`)
   `crashpad_handler.exe` all land in `build/Release/`.
2. Launch `mib_studio_qt.exe`, exercise UI, close cleanly. Check
   `app.log` for `CrashReporter initialized` and `CrashReporter shutdown`.
3. Wire a debug-only menu item to
   `CrashReporter::triggerCrashForTesting(NullDeref)` and confirm a
   `.dmp + .json` pair appears in the crash dir, with the JSON
   containing non-zero `framesProcessed` if capture was running.
4. With `MIB_SENTRY_DSN` set, confirm the event appears in Sentry
   within ~30 s with symbolicated frames.

## Files

| Kind | Path |
|---|---|
| new | `include/backend/services/CrashReporter.h` |
| new | `src/backend/services/CrashReporter.cpp` |
| new | `include/backend/diagnostics/CrashStateMirror.h` |
| new | `src/backend/diagnostics/CrashStateMirror.cpp` |
| new | `cmake/Sentry.cmake` |
| new | `knowledge_map/services/CrashReporter.md` |
| new | `knowledge_map/diagnostics/CrashStateMirror.md` |
| new | `knowledge_map/diagnostics/_MOC.md` |
| mod | `CMakeLists.txt` (PDB flags, dbghelp, sentry-native, crashpad copy) |
| mod | `src/frontend/core/main.cpp` (install reporter pre-QApplication) |
| mod | `src/backend/AppBackend.cpp` (register state mirror; seed context) |
| mod | `src/backend/services/CaptureService.cpp` (mirror writes) |
| mod | `src/backend/services/ProcessingService.cpp` (mirror writes) |
| mod | `src/backend/services/Hdf5Service.cpp` (mirror writes) |
| mod | `src/backend/services/AutofocusService.cpp` (mirror writes) |
| mod | `src/backend/playback/FrameStore.cpp` (mirror writes) |
| mod | `knowledge_map/README.md`, `services/_MOC.md`, `conventions/Logging.md`, `current-state/Recent-Work.md` |
