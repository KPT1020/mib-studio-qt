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
  small boto3 helper at `scripts/s3_upload.py`. aws CLI v2's multipart
  path omits `Content-Length` on `CreateMultipartUpload`, which
  s3.yofo.bio rejects; single-part PUT hit the proxy's body-size cap
  for the 616 MiB full installer. boto3's multipart flow always sends
  Content-Length, so both the update package and the full installer
  can upload via one code path. Workflows now `pip install conan
  boto3` together. Local runs also require boto3; install with
  `pip install boto3`.

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
