# Acquire & Record

The acquisition workflow: start the camera, frame the region of interest,
tune processing while watching the preview and monitoring charts, then
record an experiment to HDF5.

## Guided operator workflow

The bar above the tabs tracks the four operator stages — **Hardware
Preflight → Camera & Alignment → Experiment → Review** — with an
authoritative status per stage (Not started / Needs attention / Ready /
Running / Complete). Visiting a tab never completes a stage: Preflight and
Camera & Alignment complete only when their checks pass **and** you press
the confirm button on the right of the bar. The context row beneath shows
the active profile, camera, calibration, operator, storage, and overall
status at all times; click a segment to jump to the matching surface.

![Guided workflow after the operator confirms Hardware Preflight](images/workflow-preflight-confirmed.png)

## Readiness gate at Start Experiment

**Start Experiment** first shows the readiness checklist. Failed checks
that are safety-critical (camera stream, processing core, ROI) block the
start outright; other failures can be expert-overridden — each override
requires an operator name and a reason, and the full checklist plus the
override record is saved into the experiment file (`readiness_json`) for
review later.

![Experiment readiness gate shown at Start Experiment](images/dialog-experiment-readiness.png)

## Start the camera

Click **Start Camera** (main tab-bar corner). Frames start streaming from
the connected camera (or the mock folder) and the status-bar statistics
come alive. **Stop Camera** halts acquisition; statistics reset to zero.

## Overview tab — frame the ROI

![Overview tab with the live camera view and the red ROI rectangle](images/overview-live.png)

- Shows the raw live stream (display rate capped independently of the
  camera rate, so a fast camera does not overload the UI).
- The semi-transparent **red rectangle** is the region of interest (ROI):
  drag it to reposition. The ROI feeds processing and recording directly,
  and the current geometry is echoed in the status bar
  (`ROI: w x h @ (x, y)`).
- Advanced: the tab hosts the camera-side GenICam script editor. **Apply to
  Camera** stops capture while the script is applied — restart the camera
  afterwards.

## Experiment ▸ Preview — tune processing

![Experiment Preview page with the processed live view, playback controls, and the config/profiles editor](images/experiment-preview.png)

- The canvas shows the **processed** view. Overlay modes: **Off / Mask /
  Contours / Both**. While following live, cell colors mean
  **blue = target group, green = valid, red = invalid**.
- Playback controls under the canvas let you pause and scrub the recent
  frame buffer; **save buffer to disk** exports it as a single uncompressed
  AVI (ImageJ/Fiji-compatible) or a folder of TIFFs, never overwriting
  existing output.
- A **background** frame is captured automatically (or set manually from
  the playback panel); the current background shows in the sidebar preview.
- The docked **Config / Profiles** editor (right) edits the processing
  configuration — thresholds, gates, multi-image, target group — and
  manages named profiles, including catalog-published ones. Changes saved
  to the config file apply live; no restart needed. Note that several gates
  only take effect when their `enable_*` flag is on.

## Experiment ▸ Monitoring — watch the numbers

![Experiment Monitoring page with live histograms and scatter plots](images/experiment-monitoring.png)

- Live histograms (area, deformability, brightness) and scatter plots
  (e.g. deformability vs. area) over the most recent frames, with
  scroll-to-zoom.
- Live totals: valid count, invalid count, algorithm FPS, valid FPS.
- Charts accumulate only while the tab is visible, and they are a live
  preview — they are not saved with the experiment.
- Trigger bring-up controls (single and periodic test pulses) are hidden
  in normal operation — see Service/Commissioning mode below.

## Service/Commissioning mode

Engineering controls that can actuate hardware live behind
**Settings ▸ Service/Commissioning Mode**. Entering requires confirmation,
shows a persistent amber banner, and reveals the trigger test controls on
the Monitoring page; single pulses ask for confirmation (target + pulse
duration) before firing, and every actuation is logged. The mode resets to
Operator on every new session, exits automatically when an experiment
starts, and leaving it always stops a running periodic test.

![Service/Commissioning mode with the banner and trigger test controls](images/commissioning-mode.png)

## Settings dialogs

![Processing Settings dialog](images/dialog-processing-settings.png)

**Settings ▸ Processing Settings** — the full processing configuration
form: blur/threshold, validity gates, ring-ratio and target-group gating,
multi-image capture.

![Monitoring Settings dialog](images/dialog-monitoring-settings.png)

**Settings ▸ Monitoring Settings** — chart bin counts, axis ranges, and
refresh rate for the Monitoring page.

![Pixel to Micron Conversion dialog](images/dialog-pixel-to-micron.png)

**Settings ▸ Pixel-to-Micron** — the pixel→micrometre conversion factor
(default 0.4886 µm/px) used for area and derived metrics.

## Record an experiment

1. Make sure the camera is running — otherwise you'll see *"Camera must be
   running before starting an experiment."*
2. Click **Start Experiment** (Experiment tab-bar corner). A Save dialog
   asks where to write the HDF5 file (`.h5` is appended if you omit it).
   The corner indicator turns green and the flushed-frame counter starts.
3. Click **Stop Experiment** when done. The final flush runs in the
   background and the file is closed with its metadata (ROI, background,
   configuration) — wait for the indicator to return to grey before
   pulling a USB drive.

Recorded files contain the valid/invalid frame images, masks, per-frame
metrics, and the experiment metadata needed to reanalyse later — see
[Review & post-process](review-and-postprocess.md).

## Status bar statistics

| Field | Meaning |
|---|---|
| **Display** | Frames per second actually rendered in the preview |
| **Algo** | Realtime processing rate (frames/s) |
| **Valid / Invalid** | Classification rates per second |
| **Flushed(valid)** | Total valid frames written to HDF5 in the active experiment |
| **Camera** | Transport statistics from the camera (frame rate, MB/s) |
| **Ring width** | Median ring ratio of validated frames (drives autofocus) |

Rates reset to zero when an experiment starts or capture stops; totals
persist until the next experiment starts.
