# MatAllocStats

> Process-wide cv::Mat allocation accounting: a delegating
> `cv::MatAllocator` that counts every allocator-owned buffer (count +
> bytes, two relaxed atomics per alloc) before forwarding to OpenCV's
> default allocator. The direct measurement of per-frame heap churn — the
> realtime loop's Mat temporaries, per-frame clones, batch copies — which
> RSS cannot attribute (first measured: ~25k allocs/s, ~134 MB/s at 500 fps
> on 512x96 frames).

**Source:** `src/backend/diagnostics/MatAllocStats.cpp`,
`include/backend/diagnostics/MatAllocStats.h` (part of `mib_processing`,
Qt-free)
**Related:** [[PipelineTrendSampler]], [[PipelineTimingRecorder]]

## Usage

- `MatAllocStats::install()` — idempotent; wraps
  `cv::Mat::getDefaultAllocator()` and installs the counting delegate.
  Called early in `AppBackend::initialize`, before pipeline threads
  allocate. The wrapper is a leaked singleton by design (OpenCV keeps a raw
  pointer; Mats may outlive any scope).
- `allocCount()` / `allocBytes()` — cumulative monotonic counters, sampled
  at 1 Hz into `pipeline_trend.csv` (`mat_allocs`, `mat_alloc_mb`); the
  analyzer derives rates and flags a growing allocation rate at steady
  load.

## Gotchas

- Mats wrapping caller-owned memory (`data != nullptr` in the allocate
  call) are not counted — no heap allocation happened.
- Counts are process-wide (GUI overlay Mats included), not per-thread.
