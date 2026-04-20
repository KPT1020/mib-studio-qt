Title: Mock camera loading + processing parity performance fix

Context
- Reported issue: mock camera startup is slow and mock-mode processing does not reflect real-camera throughput.
- Root path reviewed: `src/camera/mock/MockCamera.cpp` and mock env wiring in `src/backend/AppBackend.cpp`.

Findings
1) Startup latency was dominated by single-threaded `preloadFrames()` decode/conversion over the full folder.
2) Per-frame pacing in `grabFrame()` used a coarse+busy spin strategy that could burn CPU and contend with realtime processing thread scheduling.
3) Frame delivery in `grabFrame()` built a temporary output frame each call, adding avoidable allocator/copy churn at high fps.

Changes implemented
- Mock preload is now parallelized with a worker pool (`hardware_concurrency` bounded by file count), preserving lexical frame order in `preloadedFrames_`.
- Mock pacing keeps timing stability while reducing CPU burn:
  - coarse sleep to near target
  - short sleep/yield in the final window instead of full busy-spin.
- `grabFrame()` now fills the output frame buffer in-place to avoid extra temporary frame construction/move churn.
- Existing mock env vars and cadence semantics are unchanged by this patch.

Measured local benchmark (same synthetic 500-frame TIFF set)
- Baseline (`main`) startup: ~154–164 ms.
- Updated startup: ~45–52 ms.
- High-rate delivery (`200 us`, ~5000 fps): maintained parity (~4990–4998 fps measured).

Notes
- This task focused on mock camera source-side overhead (load + pacing), which is where the reported gap originates.
- Realtime processing logic itself was not behaviorally changed in this task.
