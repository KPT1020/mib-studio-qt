# 2026-04-15 — Trigger-onset latency regression

> Fix: end-to-end trigger-onset latency drifted from a consistent ~400 µs
> to a variable up-to-~1 ms at a ~2 Hz cadence. Branch:
> `claude/fix-trigger-timing-bug-xGgbx`.

## Symptom

Operators observed through the triggered image (cell position drifts in
frame) that the delay from frame arrival to the TTL trigger rising edge had
become jittery up to ~1 ms. The software metric
`TriggerService::getLastOnsetUs()` only measures the `setTriggerOutput(true)`
call and isn't displayed in the UI, so this was diagnosed against end-to-end
hardware timing, not the in-UI stat.

## Diagnosis

Call chain from frame arrival to TTL edge:

```
ProcessingService::realtimeLoop                 src/backend/services/ProcessingService.cpp
  → filterProcessedImage()                                           
  → monitoringFramesMutex_ acquired                                  (3 paths)
    → targetGroupCallbackMutex_ nested                                
      → targetGroupCallback_(isTargetGroup)                           
        → TriggerService::onTargetGroupResult                         
          → triggerRequested_.store + triggerCV_.notify_one
TriggerService::triggerLoop
  → setTriggerOutput(true)
```

`monitoringFramesMutex_` is **also** held by the UI thread on a 500 ms timer
(`ExperimentMonitoringTab::onUpdate` → `getMonitoringValidFrames()` /
`getMonitoringInvalidFrames()` → `FrameRingBuffer::toVector()`), which deep-
copies up to 1000 `ProcessedFrame`s. `ProcessedFrame` carries `FilterResult`
with `std::vector<std::vector<cv::Point>> allContours` — the contour vectors
deep-copy, not refcounted. Every UI tick, the realtime thread's
target-group callback was serialised behind that snapshot, giving exactly the
symptom observed.

Secondary contributor: on every non-empty frame when
`auto_background_enabled && !experimentActive_` was true, the realtime loop
took `previousFrameMutex_` and did
`previousFrameForAutoCapture_ = blurredCurr.clone();` — a full ROI malloc +
memcpy inside the `algoStart → algoEnd` window.

## Fix

1. **Dispatch target-group + ring-ratio callbacks before taking
   `monitoringFramesMutex_`.** Copy the `std::function` out under its own
   lock (no real waiters) and invoke without any other lock held. Done in
   all three realtime paths:
   - ROI + dropFrames path (around `ProcessingService.cpp:881`)
   - Full-frame + dropFrames path (around `:1242`)
   - Every-frame path (around `:1623`)

2. **Replace per-frame `previousFrameForAutoCapture_ = blurredCurr.clone()`
   with shallow `previousFrameForAutoCapture_ = blurredCurr;`.** cv::Mat's
   refcount keeps the previous buffer alive across iterations; GaussianBlur
   allocates a fresh output each call, so no aliasing issue. All 11
   occurrences replaced.

Change 4 from the plan file (adding a new end-to-end gauge metric to
`TriggerService`) was deferred — the existing `getLastOnsetUs()` is
sufficient for the short-term observation against the oscilloscope.

## Files changed

- `src/backend/services/ProcessingService.cpp` — 3 × callback hoist, 11 × clone→shallow
- `knowledge_map/services/ProcessingService.md` — added callback ordering gotcha
- `knowledge_map/services/TriggerService.md` — clarified onset metric scope
- `knowledge_map/current-state/Recent-Work.md` — dated entry

## Verification

1. Build Debug + Release.
2. Mock-camera run: monitoring histograms still update at 2 Hz; target-group
   classifications still fire the trigger callback (spdlog trace).
3. Hardware run: with ExperimentMonitoringTab active (worst case for the old
   regression), capture triggered images and confirm the cell position is
   stable across many triggers. Compare to an oscilloscope reading on the
   TTL line.
