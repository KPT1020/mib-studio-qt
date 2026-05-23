# Run Modes

> Three executables, two camera sources, a handful of env vars.

**Related:** [[../camera/MockCamera]], [[../frontend/ConnectTab]]

## Executables

- **`mib_studio_qt.exe`** — production. Uses hardware camera via
  [[../camera/EGrabberCamera]]. No mock option in the UI.
- **`mock_studio_qt.exe`** — development. Same UI, but lets the user
  select a mock folder from the [[../frontend/ConnectTab]].

## Mock camera env vars

Read at startup (see `main.cpp` and [[../architecture/AppBackend]]):

| Variable | Default | Effect |
|---|---|---|
| `MIB_CAMERA_MODE=mock` | unset | Force mock (bypasses ConnectTab) |
| `MIB_MOCK_CAMERA_DIR=<path>` | — | Folder with PNG/TIFF/JPEG frames |
| `MIB_MOCK_CAMERA_INTERVAL_MS=<ms>` | 33 | Frame cadence |
| `MIB_MOCK_CAMERA_LOOP=true\|false` | true | Loop or stop at end |

Sample frames ship at `data/mock_frames/frame_00000.tiff`.

## Crash reporting / Sentry env vars

Read in `src/frontend/core/main.cpp` before backend startup:

| Variable | Default | Effect |
|---|---|---|
| `MIB_SENTRY_DSN` | unset | Preferred DSN for crash upload |
| `SENTRY_DSN` | unset | Fallback DSN if `MIB_SENTRY_DSN` is unset |
| `MIB_SENTRY_COMPONENT` | `mib-studio-qt/desktop` | Monorepo component label for release naming and tags |
| `MIB_SENTRY_RELEASE` | auto | Explicit release override |
| `SENTRY_RELEASE` | auto | Fallback explicit release override |
| `MIB_CRASH_ENV` | `production` / `development` | Preferred environment label |
| `SENTRY_ENVIRONMENT` | `production` / `development` | Fallback environment label |
| `MIB_GIT_SHA` | unset | Build SHA used in auto release format |

When no explicit release is set, startup computes:
`<component>@<MIB_STUDIO_QT_VERSION>+<short-sha>`.

## MLflow (test metrics only)

Test performance scripts (`scripts/empty_frame_detection.py` and the
Kedro pipeline under `scripts/kedro_frame_detection/`) log to MLflow at
`mlflow.yofo.bio`. Set credentials via env vars
`MLFLOW_TRACKING_USERNAME`, `MLFLOW_TRACKING_PASSWORD` — never hard-code.
See top-level `CLAUDE.md`.

## Troubleshooting

- `docs/howto/troubleshoot-crashes.md`
- `docs/howto/safe-start-stop-egrabber.md`
- `knowledge_map/task/qt_qpa_platform_plugin_missing_windows.md`
