# CrashReporter

> Installs process-level crash handlers, writes minidumps + JSON state
> sidecars on unrecoverable failures, and (optionally) forwards events to
> Sentry. Initialized in `main()` BEFORE Logger and AppBackend so that
> early-startup crashes are still captured.

**Source:** `src/backend/services/CrashReporter.cpp`,
`include/backend/services/CrashReporter.h`
**Related:** [[../diagnostics/CrashStateMirror]],
[[../conventions/Logging]], [[../architecture/AppBackend]]

## Responsibility

- Installs:
  - Windows `SetUnhandledExceptionFilter` → `MiniDumpWriteDump` (always
    available via `dbghelp.lib`).
  - `std::signal` handlers for SIGSEGV / SIGABRT / SIGFPE / SIGILL.
  - `std::set_terminate` for uncaught C++ exceptions.
  - `qInstallMessageHandler` to route Qt warnings/criticals into spdlog
    and forward fatal Qt messages as Sentry events.
- On crash: writes `{timestamp}-pid{N}-{reason}.dmp` (Windows) and a
  `.json` sidecar containing the current
  [[../diagnostics/CrashStateMirror]] snapshot under
  `%LOCALAPPDATA%/MIB_Studio_Qt/crashes/`.
- On startup: scans the crash dir for `.dmp` files left over from previous
  runs and (when Sentry is enabled) submits them via `sentry_capture_event`
  before renaming them `.uploaded` to prevent re-submission.

## Key APIs

```cpp
struct Config { dsn; release; environment; crashDir; databaseDir;
                installSignalHandlers; installQtMessageHandler;
                installTerminateHandler; uploadPendingOnStart; };

static bool init(const Config& cfg);
static void shutdown();
static void setTag(string_view k, string_view v);
static void setContextJson(string_view name, string_view json);
static void breadcrumb(string_view category, string_view msg,
                       string_view jsonData = {});
static void registerStateMirror(StateSnapshotFn);
static void captureMessage(string_view);
static void captureException(string_view);
static bool writeDiagnosticSnapshot(string_view reason);
```

Public free-form decoration calls (`setTag`, `breadcrumb`) are no-ops when
Sentry is not compiled in — they remain safe to sprinkle through services.

## DSN configuration

- Read from `MIB_SENTRY_DSN` env var at process start.
- Empty DSN → local-only mode: minidumps still written to disk, but never
  uploaded.
- Override environment label with `MIB_CRASH_ENV` (defaults to
  `production` for Release / `development` for Debug).

## Crash artifacts

```
%LOCALAPPDATA%/MIB_Studio_Qt/crashes/
  20260522T143015-pid12345-seh.dmp        ← Windows minidump
  20260522T143015-pid12345-seh.json       ← state snapshot
  20260522T143015-pid12345-sigsegv.json   ← (signal-handler path, no dmp on non-Win)
  20260522T143015-pid12345-terminate.json ← std::terminate path
  20260522T143015-pid12345-exception.json ← non-fatal captureException()
  *.uploaded                              ← already sent to Sentry
```

## Symbolication

Minidumps are useless without matching PDB. The CMake config now emits
`mib_studio_qt.pdb` next to the `.exe` for Release builds (via `/Zi` +
`/DEBUG /OPT:REF /OPT:ICF`). Archive each released PDB so remote crashes
can be symbolicated:

```bash
sentry-cli login
sentry-cli upload-dif --org <org> --project mib-studio-qt \
    build/Release/mib_studio_qt.exe \
    build/Release/mib_studio_qt.pdb
```

## Gotchas

- **Crashpad needs `crashpad_handler.exe` next to the EXE.** The CMake
  post-build copy step handles this when `MIB_USE_SENTRY=ON`. Without it
  Sentry silently falls back to in-process capture and loses dumps from
  non-recoverable crashes (heap corruption, stack overflow).
- The signal handler intentionally re-raises the signal with `SIG_DFL`
  so debuggers and Windows Error Reporting still see the fault.
- `registerStateMirror` MUST point to a function that does not allocate
  unbounded memory or take locks held by the crashing thread. The
  [[../diagnostics/CrashStateMirror]] uses atomics + `try_lock` to
  satisfy this.
- Worker-thread exception handling is intentionally NOT hardened in this
  change (per `task/2026-05-22-crash-monitoring.md`). Pure C++ exceptions
  inside `ProcessingService::workerLoop` will still kill that worker
  silently; Crashpad only catches SEH/native faults.
- Building with `MIB_USE_SENTRY=OFF` (or with no DSN) keeps the local
  minidump path active — useful for offline / air-gapped deployments.
