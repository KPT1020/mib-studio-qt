# Run Modes

> Three executables, two camera sources, a handful of env vars.

**Related:** [[../camera/MockCamera]], [[../frontend/ConnectTab]]

## Executables

- **`mib_studio_qt.exe`** — production. Uses hardware camera via
  [[../camera/EGrabberCamera]]. No mock option in the UI.
- **`mock_studio_qt.exe`** — development. Same UI, but lets the user
  select a mock folder from the [[../frontend/ConnectTab]].
- **`capture_processing_test.exe`** — console harness
  (`src/tests/capture_processing_test.cpp`). Uses
  [[../camera/MockCamera]] to exercise capture + processing without
  the UI.
- **`hf_pipeline_runner.exe`** — headless offline runner
  (`src/tools/hf_pipeline_runner.cpp`). Streams a folder of frames
  (typically produced by `scripts/hf_dataset_download.py` from a
  HuggingFace dataset) through the production pipeline and writes the
  standard experiment HDF5. See
  [[../../docs/howto/hf-dataset-verification.md]] and
  [[../task/2026-04-15-hf-dataset-runner]].

## Mock camera env vars

Read at startup (see `main.cpp` and [[../architecture/AppBackend]]):

| Variable | Default | Effect |
|---|---|---|
| `MIB_CAMERA_MODE=mock` | unset | Force mock (bypasses ConnectTab) |
| `MIB_MOCK_CAMERA_DIR=<path>` | — | Folder with PNG/TIFF/JPEG frames |
| `MIB_MOCK_CAMERA_INTERVAL_MS=<ms>` | 33 | Frame cadence |
| `MIB_MOCK_CAMERA_LOOP=true\|false` | true | Loop or stop at end |

Sample frames ship at `data/mock_frames/frame_00000.tiff`.

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
