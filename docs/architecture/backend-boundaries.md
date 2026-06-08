# Backend Boundaries

The backend owns application behavior and exposes stable interfaces to frontend
adapters. The Qt frontend and future Tauri bridge should call backend services
or the backend facade rather than duplicating service logic.

## Backend Responsibilities

- Camera lifecycle and frame acquisition.
- Mock camera and hardware camera adapters.
- Frame processing and object/metadata calculations.
- HDF5 recording and playback.
- SQLite-backed metadata persistence.
- Long-running service coordination through `backend::AppBackend`.
- Frontend-neutral callbacks and events.

## Frontend Responsibilities

- QWidget/QMainWindow based presentation.
- Dialogs, tabs, controllers, and user interaction.
- Conversion between backend data and `QImage`/`QPixmap`.
- UI-thread dispatch and rendering.
- Frontend-specific resource loading.

## Public API Rules

Backend public headers may expose:

- Standard C++ types.
- Backend domain types from `include/backend`.
- OpenCV types where the existing processing API already uses `cv::Mat`.
- Qt Core utility types only when a service truly requires Qt infrastructure.

Backend public headers should not expose:

- `QWidget`
- `QMainWindow`
- `QImage`
- `QPixmap`
- Frontend dialogs, tabs, widgets, models, or resources.

Qt conversion belongs at the adapter boundary:

```text
backend cv::Mat / frame data
        |
        v
Qt adapter converts to QImage/QPixmap
        |
        v
Qt widgets render the image
```

The background auto-capture path follows this rule: processing emits a
backend-owned `cv::Mat`, `backend::AppBackend` forwards it through a standard
callback, and `MainWindow` converts it to `QImage` before updating Qt widgets.

## Bridge Boundary

The bridge layer depends on `mib_backend`; `mib_backend` does not depend on the
bridge. Bridge command and event types live under `include/bridge` so Qt and
future Tauri code can share names without sharing UI implementation.

```text
Frontend command:
  start_camera
  stop_camera
  start_recording
  stop_recording
  update_processing_settings
  load_recording
  seek_playback

Backend event:
  frame_ready
  camera_status_changed
  recording_status_changed
  processing_result_ready
  playback_position_changed
  error
```
