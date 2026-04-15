# FrameStore

> Thread-safe ring buffer for in-memory frame history. Shared by
> [[../services/CaptureService]] (producer), [[../services/ProcessingService]]
> (realtime consumer), and [[../services/PlaybackService]] (UI consumer).

**Source:** `src/backend/playback/FrameStore.cpp`,
`include/backend/playback/FrameStore.h`
**Related:** [[../architecture/AppBackend]], [[../services/PlaybackService]]

## Capacity

- Header default constructor: `FrameStore(capacity = 512)`.
- **Actual runtime capacity**: `5000`, set in
  `AppBackend::initialize` (`src/backend/AppBackend.cpp:97`).
- Resizable at runtime via `resize(newCapacity)`.

## Frame type

```cpp
struct Frame {
    uint64_t width, height;
    uint64_t pixelFormat;   // PFNC code
    size_t linePitch;       // bytes per line
    uint64_t timestamp;     // device ticks OR nanoseconds (source-dependent)
    std::vector<uint8_t> data;
};
```

## Absolute indexing

Write-index is monotonic (never wraps). Consumers must query the current
window:

- `earliestAvailableIndex()` / `latestAvailableIndex()`
- `availableCount()`, `totalWritten()`
- `getAvailableRange() → IndexRange { start, end }`
- `getAvailableTimestampRange(TimestampRange&)`

## Push / query APIs

```cpp
void pushFrame(src, size, w, h, linePitch, pixelFormat, timestamp,
               captureSteadyNs = 0);  // last arg: steady_clock nanos at grab

bool getLatest(Frame& out) const;
bool getByWriteIndex(uint64_t idx, Frame& out) const;
bool getByWriteIndexROI(uint64_t idx, int roiX, int roiY, int roiW, int roiH,
                        Frame& out) const;  // avoids full-frame copy

// Sideband: fetch the steady_clock capture nanos recorded at grab time.
// Used by the trigger pipeline to schedule pulse onset at a fixed delay
// from capture, independent of processing latency.
bool getCaptureSteadyNs(uint64_t idx, uint64_t& outNs) const;

bool saveFramesToDisk(dir, filterFn = nullptr) const;       // all frames
bool saveFramesToDisk(dir, startIdx, endIdx, filterFn);     // by index
bool saveFramesToDisk(dir, startTs, endTs, /*useTs=*/true, filterFn);

bool resize(size_t newCapacity);
size_t estimateMemoryBytesForCapacity(size_t capacity) const;
```

## Sideband capture timestamps

Parallel to `ring_` the store keeps a `std::vector<uint64_t>
captureSteadyNs_` of the same capacity, indexed identically. Each entry
is a **predicted CPU-clock capture timestamp** produced by
[[../services/CaptureService]]: `frame.timestamp` (hardware ns, periodic,
jitter-free) plus an EMA-smoothed hw→CPU offset. This gives the trigger
pipeline a jitter-free anchor so pulses land on the hardware's periodic
capture grid even under variable processing load. 0 means "not
recorded" or "no hardware timestamp available" (in which case the
producer falls back to raw `steady_clock::now()`).

This is intentionally kept out of `struct Frame` to avoid rippling through
the many consumers that persist `Frame` (HDF5 writes, TIFF saves, playback
copies). The sideband is updated and read under the same `mutex_` as
`ring_`.

Consumed by [[../services/ProcessingService]]'s realtime loop, which
passes the timepoint through the `TargetGroupCallback` to
[[../services/TriggerService]].

## Frame filter (recording mode)

```cpp
using FrameFilter = std::function<bool(const Frame&)>;  // true = SKIP
void setFrameFilter(FrameFilter);
void clearFrameFilter();
bool hasFrameFilter() const;

uint64_t totalFiltered() const;  // count of skipped frames
void resetFilteredCount();
```

Used by `AppBackend::startFrameRecording` to drop empty frames before they
hit the ring.

## Threading

Single `std::mutex` serialises `pushFrame` / `get*` / `resize`. Not lock-
free, but per-frame work is a `std::vector<uint8_t>` move.

## Gotchas

- If a consumer holds onto a `getByWriteIndex` copy for longer than
  `capacity / fps` seconds, its data is still valid (copy) but the index
  may fall off the available range.
- `pushFrame` copies bytes — if you profile and see allocator pressure,
  investigate here first.
