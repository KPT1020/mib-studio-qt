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
| `MIB_DISABLED_SERVICES=<csv>` | unset | Disable startup paths (`sqlite,hdf5,processing,yolo,autofocus,trigger,capture/camera,playback,auto_update,all`) |

Sample frames ship at `data/mock_frames/frame_00000.tiff`.

The GUI Settings action **Boot Service Toggles...** stores a persisted
`QSettings` value (`Startup/DisabledServices`) that `main.cpp` maps into
`MIB_DISABLED_SERVICES` before backend startup (unless the env var is already set externally).

## MLflow (test metrics only)

Test performance scripts (`scripts/empty_frame_detection.py` and the
Kedro pipeline under `scripts/kedro_frame_detection/`) log to MLflow at
`mlflow.yofo.bio`. Set credentials via env vars
`MLFLOW_TRACKING_USERNAME`, `MLFLOW_TRACKING_PASSWORD` — never hard-code.
See top-level `CLAUDE.md`.

## Backend-only validation mode (build/test)

For backend CI loops without Qt Widgets/Charts frontend binaries:

- Configure/build preset: `linux-backend-only`, `linux-backend-only-build`
- Test preset: `linux-backend-only-test`
- Backend tests can be selected by label:
  `ctest --preset linux-backend-only-test -L backend`

## Troubleshooting

- `docs/howto/troubleshoot-crashes.md`
- `docs/howto/safe-start-stop-egrabber.md`
- `knowledge_map/task/qt_qpa_platform_plugin_missing_windows.md`

## Backend crash-durability validation (headless)

Use `hdf5_abrupt_stop_tool` in backend-only Linux builds to run SIGKILL-style
HDF5 durability checks and strict frame-identity validation against mock source
frames (input frame index -> saved frame index).

```bash
# writer modes (run in one shell; kill externally)
hdf5_abrupt_stop_tool run-experiment /tmp/test_exp.h5
hdf5_abrupt_stop_tool run-recording /tmp/test_rec.h5

# post-kill validators
hdf5_abrupt_stop_tool check-checkpoint /tmp/test_exp.h5
hdf5_abrupt_stop_tool check-experiment /tmp/test_exp.h5
hdf5_abrupt_stop_tool check-recording /tmp/test_rec.h5
```

Each `check-*` mode writes a side-by-side preview to `<file>.preview.png`
(input/reference vs saved frame) and enforces non-empty recovery checkpoint
files plus bit-identical frame equality.
