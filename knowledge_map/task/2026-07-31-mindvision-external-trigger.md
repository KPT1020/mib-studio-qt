# 2026-07-31 — MindVision external trigger + strobe sync + pulse generator

> Tracking note for epic #323 (sub-issues #324 camera, #325 pulse generator,
> #326 GUI, #327 verification). Branch
> `claude/mindvision-external-trigger-5d1ohk`.

## Goal

Move MindVision acquisition from free-run to triggered capture: an external
TTL pulse train (Zhongsheng 脉冲频率与占空比输出 RS485 module) triggers each
exposure, with the camera strobe synced to exposure for illumination.
Software trigger (mode 1) supported for bench testing without the pulse
source.

## What was implemented

- **Config schema** (`MindVisionConfig.h`): `ext_trig_signal_type` [0,4],
  `ext_trig_jitter_us` / `acq_trigger_delay_us` [0,1e6], `trigger_count`
  [1,1000]; `strobe_mode` clamp widened [0,2]→[0,3].
- **Shared apply helper** `MindVisionApply.{h,cpp}` —
  `applyConfigToHandle(hCamera, cfg, firstError)`; both
  `MindVisionCamera::applyJsonConfig` and
  `CameraControlService::applyMindVisionJsonToCamera` route through it
  (drift closed). New setters go right after `CameraSetTriggerMode`, before
  `CameraPlay`. No `API_LOAD_MAIN` in this TU.
- **softTrigger()**: `ICamera` default-false virtual →
  `MindVisionCamera::softTrigger` (`CameraSoftTrigger` under `stateMutex_`)
  → `CaptureService::softTriggerActiveCamera` (under `cameraMutex_`; same
  lock order as `stop()`) → `AppBackend::softTriggerCamera` → facade
  `CameraCommandAction::SoftTriggerCamera`.
- **checkDeviceHealth**: skips the frame-consuming probe when
  `configuredTriggerMode_ != 0` (probe would eat a triggered frame; timeout
  is normal idle). `CameraConnectTest` deliberately NOT used — symbol
  presence in deployed `CameraApiLoad.h` function tables is unverifiable
  from CI.
- **Bug fixes**: `grabFrame` copies `outBuffer_` under `stateMutex_`
  (use-after-free vs `stop()`); `CameraSetIspOutFormat(MONO8)` unconditional
  (color-sensor buffer overrun). Deferred: `configureTriggerOutput`
  index-before-open (dead branch).
- **PulseGeneratorService** (Modbus RTU via `ModbusRtu.h`, pump-service
  pattern): connect/verify (FC03 read, no writes), `setFrequency` (FC16,
  u32=Hz×100 high-word-first, clamp 400–40k), `setDutyCycle` (FC06,
  u16=%×100), `setOutputEnabled` (duty-0 gating — module has no run/stop
  register). Owned by `AppBackend::pulseGenerator()`.
- **GUI**: new ConfigTabs tab "MindVision config (mindvisionConfig.json)" —
  editor + Reset/Save/Apply/Soft Trigger/Browse/Clear plus a pulse-generator
  group (COM/baud/addr, channel, freq, duty, Set/Start/Stop).
- **Sample config**: `resources/defaults/mindvisionConfig.json` + qrc entry.
- **Tests**: `backend.pulse_generator_frame` (manual §2.3 known-answer
  frames), `backend.mindvision_config` cases 10–15 (new fields, clamps),
  facade boundary test soft-trigger negative cases.

## Verification status

- Linux stub build (backend + frontend): green; 44/44 backend tests pass;
  `scripts/check_docs.py` clean.
- **Windows real-SDK build: NOT yet done** (CI never compiles it). Before
  merging: build with `-DMIB_ENABLE_MINDVISION=ON`, grep the installed
  `CameraApiLoad.h` for `CameraSetExtTrigSignalType`,
  `CameraSetExtTrigJitterTime`, `CameraSetTriggerDelayTime`,
  `CameraSetTriggerCount`, `CameraSoftTrigger`.

## Hardware bring-up checklist (#327)

1. Wiring: module Y1/COM− → camera trigger input (check camera I/O voltage
   spec vs module 3.3/5 V jumper); camera strobe out → illumination/scope;
   USB-RS485 adapter → A/B terminals.
2. Verify generator output on a scope at the set frequency/duty before
   connecting the camera.
3. Free-run regression: default config unchanged behavior.
4. Mode 1: capture idles without frames (health check must NOT kill capture
   after >5 s); each Soft Trigger press → exactly `trigger_count` frames.
5. Mode 2: frame rate tracks generator frequency; Stop (duty 0) → frames
   stop; timed frame count == pulse count; rising vs falling edge; jitter
   filter; `acq_trigger_delay_us` visible on scope (strobe vs trigger edge).
6. Strobe scope check per mode (auto/manual width+delay/polarity).
7. Lifecycle: apply-while-running restarts triggered mode; 10× rapid
   start/stop under external trigger; soft-trigger while stopped → clean
   error dialog.
8. Record results here.
