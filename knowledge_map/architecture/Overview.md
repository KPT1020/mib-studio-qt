# Architecture — Overview

> C++17 / Qt6 Widgets + Charts application for real-time microscopy image
> capture, processing, and analysis.

## Layers (top → bottom)

```
┌──────────────── Frontend (Qt Widgets, src/frontend/) ───────────────┐
│  MainWindow → tabs (Connect, Preview, Config, Monitoring, Review,  │
│                     Nanopositioner, SyringePump, Overview)          │
│  Controllers: CameraController, ExperimentController                │
└────────────────────────────┬────────────────────────────────────────┘
                             │ service getters
┌────────────────────────────▼────────────────────────────────────────┐
│                 AppBackend  (src/backend/AppBackend.cpp)            │
│                 composition root — owns every service + FrameStore   │
└────────────────────────────┬────────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────────┐
│                       Services (src/backend/services/)              │
│  Capture · Processing · Hdf5 · Playback · Autofocus · CameraControl │
│  Trigger · SyringePump · Yolo · Recorder · Sqlite                   │
└────────────────────────────┬────────────────────────────────────────┘
                             │ grabFrame / config
┌────────────────────────────▼────────────────────────────────────────┐
│         Camera abstraction (src/backend/camera/) — ICamera interface        │
│            EGrabberCamera (hardware) · MockCamera (dev)             │
└─────────────────────────────────────────────────────────────────────┘
```

## Core runtime objects

- **`AppBackend`** — see [[AppBackend]].
- **`FrameStore`** — shared ring buffer (capacity **5000**), shared between
  capture, processing, and playback. See [[../data-model/FrameStore]].

## Third-party stack

See [[../build-and-run/Dependencies]]. Key libs: Qt6, OpenCV, HDF5, spdlog,
ONNX Runtime, nlohmann_json, Euresys EGrabber SDK (hardware camera), Coremor
DLL (nanopositioner).

## Reading order

See [[../Agent-Onboarding]].
