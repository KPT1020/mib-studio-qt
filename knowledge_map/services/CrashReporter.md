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

- **Handler ownership depends on whether Sentry came online.** When
  `sentry_init` succeeds, Sentry's own backend (Crashpad on Windows,
  inproc elsewhere) keeps the process crash handlers, and the local
  `.dmp`/`.json` sidecars are written from the `on_crash` hook instead.
  `SetUnhandledExceptionFilter` / `std::signal` do NOT chain, so
  installing our own handlers on top of Sentry's (the pre-2026-07 code
  did) disables Sentry crash capture entirely.
- When Sentry is NOT live (no DSN, fetch failed, `MIB_USE_SENTRY=OFF`),
  installs:
  - Windows `SetUnhandledExceptionFilter` → `MiniDumpWriteDump` (always
    available via `dbghelp.lib`).
  - `std::signal` handlers for SIGSEGV / SIGABRT / SIGFPE / SIGILL.
- Always installs:
  - `std::set_terminate` for uncaught C++ exceptions. The handler
    extracts `what()` from the active exception, logs it, writes a
    `-terminate.txt` sidecar with the message, and (when Sentry is live)
    sends a fatal event with `sentry_flush` before aborting.
  - `qInstallMessageHandler` to route Qt warnings/criticals into spdlog
    and forward fatal Qt messages as Sentry events (flushed before Qt
    aborts).
- On crash: writes `{timestamp}-pid{N}-{reason}.dmp` (Windows) and a
  `.json` sidecar containing the current
  [[../diagnostics/CrashStateMirror]] snapshot under
  `%LOCALAPPDATA%/MIB_Studio_Qt/crashes/`. Reason `crash` = written by
  the Sentry `on_crash` hook; `seh`/`sig*`/`terminate` = local handlers.
- On startup: scans the crash dir for `.dmp` files left over from previous
  runs and (when Sentry is live) uploads the actual dump via
  `sentry_capture_minidump` (with the `.json` sidecar attached as the
  `state_snapshot` extra) before renaming them `.uploaded`.
  `-crash.dmp` files are retired without re-capture — Crashpad already
  reported those crashes live.

## Key APIs

```cpp
struct Config { dsn; release; environment; crashDir; databaseDir;
                handlerPath;  // crashpad_handler.exe, pinned by main()
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
  20260522T143015-pid12345-crash.dmp      ← Sentry on_crash hook (Crashpad also uploaded it)
  20260522T143015-pid12345-terminate.json ← std::terminate path
  20260522T143015-pid12345-terminate.txt  ← what() of the uncaught exception
  20260522T143015-pid12345-exception.json ← non-fatal captureException()
  *.uploaded                              ← already sent to Sentry (or retired)
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

- **Never install a SEH filter or signal handler after `sentry_init`.**
  They replace Sentry's handlers without chaining and silently kill
  crash capture. Local sidecars belong in the `on_crash` hook; local
  handlers are for the Sentry-inactive path only. `init()` enforces this.
- **Crashpad needs `crashpad_handler.exe` next to the EXE.** The CMake
  post-build copy step handles this when `MIB_USE_SENTRY=ON`, and
  `main.cpp` now pins the path explicitly via `Config::handlerPath` so
  startup does not depend on the working directory.
- `main()` must call `shutdown()` (or `captureException`, which flushes)
  on every exit path — Sentry's transport is asynchronous and events
  captured right before `return`/abort are lost without a flush.
- The signal handler intentionally re-raises the signal with `SIG_DFL`
  so debuggers and Windows Error Reporting still see the fault.
- `registerStateMirror` MUST point to a function that does not allocate
  unbounded memory or take locks held by the crashing thread. The
  [[../diagnostics/CrashStateMirror]] uses atomics + `try_lock` to
  satisfy this.
- Backend worker-thread loops (capture, processing, trigger, autofocus,
  frame recording) wrap their bodies in try/catch and report escaping
  exceptions via `captureException`; the enriched terminate handler is
  the backstop for any thread that still lets one escape.
- Building with `MIB_USE_SENTRY=OFF` (or with no DSN) keeps the local
  minidump path active — useful for offline / air-gapped deployments.
- Regression tests: `tests/backend/crash_reporter_terminate_test.cpp`
  (terminate sidecars) and `tests/backend/logger_init_fallback_test.cpp`
  (logging fallback/idempotency).
