# Sentry crash-reporting setup

This guide covers the *one-time* configuration needed to wire the build
pipeline to a Sentry project. The application-side integration
([CrashReporter](../../knowledge_map/services/CrashReporter.md),
[CrashStateMirror](../../knowledge_map/diagnostics/CrashStateMirror.md),
CMake `cmake/Sentry.cmake`) already exists in the repo — only the
project-specific values (DSN, org slug, auth token, self-hosted URL)
need to be supplied.

The integration targets a **self-hosted Sentry instance**, but the same
steps work against sentry.io — only the `SENTRY_URL` secret changes.
For the production self-hosted server runbook, see
[`deploy/sentry/README.md`](../../deploy/sentry/README.md).

## 1. Create the Sentry project

On your self-hosted Sentry instance (or sentry.io):

1. Create a new project with the **Native** platform.
2. Note the project slug (for this repo: `mib-studio-qt`) and the org
   slug (for the self-hosted instance: `sentry`).
3. Copy the **DSN** from Project Settings → Client Keys (DSN). It will
   look like
   `https://<public_key>@<host>/<project_id>`.

## 2. Create an auth token

Sentry → User Settings → Auth Tokens → Create New Token. Scopes
required:

- `project:read`
- `project:releases`
- `project:write`
- `org:read`

Save the token; you cannot view it again after creation.

## 3. Add GitHub Actions secrets

In `gavinlouuu-kpt/mib-studio-qt` → Settings → Secrets and variables → Actions,
add:

| Secret | Example value | Used for |
|---|---|---|
| `SENTRY_DSN` | `https://<public_key>@sentry.yofo.bio/<project_id>` | Baked into the installer; sets `MIB_SENTRY_DSN` on installed machines |
| `SENTRY_URL` | `https://sentry.yofo.bio` | Base URL of self-hosted instance (omit for sentry.io) |
| `SENTRY_ORG` | `sentry` | Org slug for `sentry-cli` |
| `SENTRY_PROJECT` | `mib-studio-qt` | Project slug for `sentry-cli` |
| `SENTRY_AUTH_TOKEN` | `sntrys_…` | Token from step 2 |

If `SENTRY_AUTH_TOKEN` is unset, the build workflow skips symbol upload
cleanly (the installer still works, but remote crashes won't be
symbolicated).

If `SENTRY_DSN` is unset, the installer ships without a baked DSN and
crashes fall back to local-only (`.dmp` + `.json` written to
`%LOCALAPPDATA%/MIB_Studio_Qt/crashes/`, never uploaded).

## 4. Trigger a release build

Run the **Build Windows** workflow with `mode=release`. The pipeline
will:

1. Pass `-DMIB_SENTRY_DSN=...` to CMake so the InnoSetup compiler emits
   a `[Registry]` entry writing `HKLM\…\Environment\MIB_SENTRY_DSN`.
2. Build `mib_studio_qt.exe` + `mib_studio_qt.pdb` +
   `crashpad_handler.exe`.
3. Run `sentry-cli debug-files upload --include-sources build\Release`
   so the PDBs land in Sentry indexed by debug-id.
4. Run `sentry-cli releases new/finalize mib_studio_qt@<version>` so
   events from runtime `release="mib_studio_qt@<version>"` group under
   that release in the dashboard.

Look for these log lines in the workflow run:

```
Sentry DSN injected into installer (env=production)
OK: build\Release\mib_studio_qt.exe (X.X MB)
OK: build\Release\mib_studio_qt.pdb (Y.Y MB)
OK: crashpad_handler.exe present
```

## 5. Verify on a clean machine

After installing the produced setup `.exe`:

```powershell
# Confirm the env var was set
[System.Environment]::GetEnvironmentVariable('MIB_SENTRY_DSN','Machine')

# Confirm crashpad_handler shipped
Get-Item 'C:\Program Files\MIB Studio Qt\crashpad_handler.exe'
```

To force a test crash (Debug builds only), wire a hidden menu entry to
`CrashReporter::triggerCrashForTesting(FaultKind::NullDeref)`. A
matching event should appear in Sentry within ~30 s with symbolicated
frames and a `state_snapshot` extra field summarizing live service
state.

Performance monitoring is also enabled for Sentry builds. Release builds
default to a transaction sample rate of `0.20`; Debug builds default to `1.0`.
Override either value on a test machine with:

```powershell
[Environment]::SetEnvironmentVariable('MIB_SENTRY_TRACES_SAMPLE_RATE','1.0','Machine')
```

Restart the app after changing the value. In Sentry, check **Performance**
for transactions such as `experiment.stop`, `hdf5.append_frames`,
`hdf5.close_file`, `hdf5.review_load`, `hdf5.read_images_range`, and
`playback.degraded`.

## Local development

For local debug builds you don't usually want events sent to production
Sentry. Two options:

- Leave `MIB_SENTRY_DSN` unset; the reporter runs in local-only mode
  and just writes `.dmp` + `.json` under
  `%LOCALAPPDATA%/MIB_Studio_Qt/crashes/`.
- Set `MIB_SENTRY_DSN` to a dev-only project, and
  `MIB_CRASH_ENV=development` so events are tagged distinctly.

## Symbol upload by hand

If you need to upload symbols outside CI (e.g. for a hotfix built
locally):

```powershell
$env:SENTRY_AUTH_TOKEN = "sntrys_..."
$env:SENTRY_URL = "https://sentry.yofo.bio"      # omit for sentry.io
$env:SENTRY_ORG = "sentry"
$env:SENTRY_PROJECT = "mib-studio-qt"

sentry-cli debug-files upload --include-sources build\Release
sentry-cli releases new "mib_studio_qt@1.2.3"
sentry-cli releases finalize "mib_studio_qt@1.2.3"
```

## Related

- [troubleshoot-crashes.md](troubleshoot-crashes.md) — what to look at when a crash happens
- [deploy/sentry/README.md](../../deploy/sentry/README.md) — self-hosted server runbook
- [CrashReporter (vault)](../../knowledge_map/services/CrashReporter.md)
- [CrashStateMirror (vault)](../../knowledge_map/diagnostics/CrashStateMirror.md)
