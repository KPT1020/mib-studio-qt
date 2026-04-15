# Recent Work

> Snapshot of recently merged features and fixes, as of 2025-11 / 2025-12.
> Refresh from `git log --oneline -20` when outdated.

## Features shipped

- **Dual syringe pump control** (PR #58) —
  [[../services/SyringePumpService]] + [[../frontend/SyringePumpTab]] +
  `SyringePumpSettingsDialog`. Modbus RTU over two COM ports (Sample +
  Sheath).
- **Multi-image recording mode** (PR #57) — [[../services/ProcessingService]]
  `multi_image_enabled`, `ProcessedFrame::seriesImages`. HDF5 gets a
  4D `series_images` dataset. [[../frontend/HdfReviewTab]] grew
  `readSeriesImagesByIndex` support.
- **Parameter tuning + monitor overlay** (PR #55) — bidirectional sync
  between the param-tuning panel and the config table; see
  [[../frontend/ConfigTabs]].
- **Ring ratio configuration and validation** — added
  `ring_ratio_min/max` + `enable_ring_ratio_check` to
  `ProcessingConfig`; [[../services/AutofocusService]] consumes the
  same values via callback.
- **Review-tab crash fix + Close File button** (PR #61) — releases HDF5
  handles cleanly; see [[../frontend/HdfReviewTab]].

## Recent fixes

- **2026-04-15** — **Decoupled trigger onset from processing-pipeline
  latency.** Previously [[../services/TriggerService]] fired its DO pulse
  immediately upon [[../services/ProcessingService]]'s target-group
  callback, so onset-from-capture tracked variable processing cost (1–10 ms
  jitter per frame). Now the trigger schedules pulses on a deadline queue
  using a steady_clock capture timestamp recorded in
  [[../services/CaptureService]] right after `grabFrame()`, propagated
  via a new sideband ring in [[../data-model/FrameStore]]. New
  `setTriggerDelayUs` knob (default 0, regression-safe) exposed in
  [[../frontend/ExperimentMonitoringTab]] as `triggerDelaySpin`. New
  metrics: `getLastRealizedDelayUs`, `getLastSlipUs`,
  `getDroppedTriggers`.
- **2026-04-15** — **Anchored trigger scheduling on hardware clock to
  eliminate CPU-side capture jitter.** CaptureService now tracks a slow
  EMA of `cpu_ns - hw_ns` (weight 1/64) and pushes a **predicted** CPU
  capture time (`frame.timestamp` + smoothed offset) into FrameStore's
  sideband instead of raw `steady_clock::now()`. This makes trigger
  onsets land on the camera's jitter-free periodic grid regardless of
  processing-pipeline load. Sudden hw-clock jumps (> 100 ms) trigger a
  re-bootstrap with a WARN log. New `CaptureStats` fields
  `clockOffsetNs` / `lastRawOffsetNs` expose the correction for
  diagnostics.
- Param tuning panel now stays in sync with the config table both
  directions.
- HDF Review tab: fixed dangling pointer crash on file close.
- **2026-04-15** — `package_installer` / `package_installer_update`
  now pass `/O${INSTALLER_OUTPUT_DIR}` to ISCC so installers land in
  `build/dist/` as CMake and `build-windows.yml` expect. Without it,
  Inno Setup honored `OutputDir=build\dist` from the `.iss` file
  relative to `resources/installers/`, writing to
  `resources/installers/build/dist/` and breaking the CI artifact
  upload step.
- **2026-04-15** — `build-windows.yml` beta path now uses
  `gh release create --target <sha>` instead of `git tag` + create.
  The local tag was never pushed, so `gh release create` refused to
  bind the release to it. `--target` makes gh create the tag
  server-side atomically.
- **2026-04-15** — `publish-update.ps1` switched from `aws` CLI to a
  small boto3 helper at `scripts/s3_upload.py`. Both CLIs omit
  `Content-Length` on `CreateMultipartUpload` and s3.yofo.bio
  rejects that; the helper registers a `before-send.s3` event hook
  that forces `Content-Length` onto every S3 request before it goes
  out. Workflows now `pip install conan boto3`. Local runs also
  require boto3 (`pip install boto3`).

## Historical tasks worth knowing about

See [[Task-Log-Index]] for the full list. Highlights:

- Safe start/stop of EGrabber (`-1012` shutdown errors).
- Preview capped at 60 Hz (configurable).
- Nanopositioner tab moved into the Config area.
- Live config reload propagates to all services.
- Config profiles (Save/Load per-user settings) — planned.
- Review system scalable to 2 GB files (lazy reads + virtualization).

## Branch context

Active development branches use the `claude/` prefix (e.g.
`claude/create-agent-onboarding-docs-J9j66`). Main is the integration
branch.
