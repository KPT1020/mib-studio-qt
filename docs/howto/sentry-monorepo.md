# Sentry setup for monorepo-style releases

This repository uses `sentry-native` for crash reporting in the Qt desktop app
and `sentry-cli` for release metadata + symbol upload.

The setup below keeps release names unique across a shared Sentry organization
and works when multiple apps/services are managed from one repository.

## 1) Build with sentry-native enabled

Sentry is enabled by default (`MIB_USE_SENTRY=ON`), but can be disabled at
configure time:

```bash
cmake -S . -B build -DMIB_USE_SENTRY=ON
cmake --build build --config Release
```

If `sentry-native` cannot be fetched, the app still runs in local-only crash
mode (minidumps + JSON sidecars on disk).

## 2) Configure runtime environment

Set environment variables before launching the app:

| Variable | Required | Example | Purpose |
|---|---|---|---|
| `MIB_SENTRY_DSN` or `SENTRY_DSN` | Yes (for upload) | `https://...` | Event upload destination |
| `MIB_SENTRY_COMPONENT` | Recommended | `mib-studio-qt/desktop` | Component prefix in release names |
| `MIB_SENTRY_RELEASE` or `SENTRY_RELEASE` | Optional | `mib-studio-qt/desktop@1.2.0+abc123def456` | Explicit release override |
| `MIB_CRASH_ENV` or `SENTRY_ENVIRONMENT` | Optional | `production` | Environment label |
| `MIB_GIT_SHA` (or CI SHA vars) | Optional | `abc123def456` | Build metadata for auto release name |

If no explicit release is provided, the app computes:

```text
<component>@<app-version>+<git-sha>
```

Example:

```text
mib-studio-qt/desktop@1.0.0+1a2b3c4d5e6f
```

## 3) Create/finalize release metadata (monorepo-safe)

Use the helper script:

```bash
scripts/sentry-release-monorepo.sh [debug-file-paths...]
```

The script:

1. Builds a release name using monorepo-safe naming (`component@version+sha`).
2. Creates the release for one or more projects.
3. Associates commits (`--auto`, fallback to `--local`).
4. Optionally uploads debug files passed as arguments.
5. Finalizes the release and records a deploy for the selected environment.

### Example (single project)

```bash
export SENTRY_ORG=my-org
export SENTRY_PROJECT=mib-studio-qt
export SENTRY_AUTH_TOKEN=...
export MIB_SENTRY_COMPONENT=mib-studio-qt/desktop
scripts/sentry-release-monorepo.sh build/Release/mib_studio_qt.exe build/Release/mib_studio_qt.pdb
```

### Example (multiple projects in one repo)

```bash
export SENTRY_ORG=my-org
export SENTRY_PROJECTS=mib-studio-qt,mib-studio-tools
export SENTRY_AUTH_TOKEN=...
export MIB_SENTRY_COMPONENT=mib-studio-qt/desktop
scripts/sentry-release-monorepo.sh build/Release/mib_studio_qt.exe build/Release/mib_studio_qt.pdb
```

## 4) Verify runtime alignment

Ensure the app process uses the same `SENTRY_RELEASE` value emitted by the
release script. This keeps crash events, commits, and symbols linked to the
same release entity in Sentry.
