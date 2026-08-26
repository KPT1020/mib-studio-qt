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
  with a `crash_dump_file` tag (the dump filename) for correlation with the
  Crashpad-managed minidump event, before renaming them `.uploaded` to
  prevent re-submission.

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
- Override performance transaction sampling with
  `MIB_SENTRY_TRACES_SAMPLE_RATE` (`0.0` to `1.0`; defaults are `0.20`
  for Release and `1.0` for Debug).

## Performance Monitoring

CrashReporter enables Sentry Performance transactions when Sentry is
configured. The current instrumentation covers:

- `experiment.stop` (`ui.action`) — total time spent stopping/saving an
  experiment.
- `hdf5.append_frames` (`hdf5.write`) — HDF5 append duration with valid,
  invalid, and multi-image series counts/timings in `perf_data`.
- `hdf5.close_file` (`hdf5.close`) — HDF5 close/flush duration.
- `playback.degraded` (`ui.render`) — throttled to at most once per minute
  when display FPS drops below 30, average latency exceeds 250 ms, dropped
  frames are detected, or overlay compute exceeds 30 ms.

In Sentry, look under **Performance** or filter transactions by
`release:mib_studio_qt@<version>` and `environment:production`.

### How the DSN reaches production installs

The release pipeline injects the DSN at three layers:

1. **CMake** — `cmake -DMIB_SENTRY_DSN=...` is set by the
   `Build Windows` workflow (from the `SENTRY_DSN` repo secret) when
   running the `CMake configure` step.
2. **InnoSetup** — CMake forwards `MIB_SENTRY_DSN` to ISCC via
   `/DSentryDSN=...`. The `[Registry]` section in
   `resources/installers/mib-studio-qt.iss` writes a system-wide
   `HKLM\…\Environment\MIB_SENTRY_DSN` value so every process spawned
   after install picks it up.
3. **Runtime** — `main.cpp` reads `MIB_SENTRY_DSN` (and the optional
   `MIB_CRASH_ENV`) via `qgetenv` and passes them into
   `CrashReporter::init`.

Operator setup (org slug, auth token, self-hosted URL) is documented in
[`docs/howto/sentry-setup.md`](../../docs/howto/sentry-setup.md).

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

Minidumps are useless without matching PDB. The CMake config emits
`mib_studio_qt.pdb` next to the `.exe` for Release builds (via `/Zi` +
`/DEBUG /OPT:REF /OPT:ICF`).

The `Build Windows` GitHub Actions workflow runs
`sentry-cli debug-files upload --include-sources build\Release` on
every release/beta build, so symbols are pushed automatically when
`SENTRY_AUTH_TOKEN` is present. The workflow then creates a Sentry
release named `mib_studio_qt@<version>`, matching the `release` field
set at runtime by `main.cpp`.

For manual / hotfix uploads outside CI:

```powershell
$env:SENTRY_AUTH_TOKEN = "sntrys_..."
$env:SENTRY_URL = "https://sentry.yofo.bio"      # omit for sentry.io
$env:SENTRY_ORG = "sentry"
$env:SENTRY_PROJECT = "mib-studio-qt"

sentry-cli debug-files upload --include-sources build\Release
sentry-cli releases new "mib_studio_qt@$version"
sentry-cli releases finalize "mib_studio_qt@$version"
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
