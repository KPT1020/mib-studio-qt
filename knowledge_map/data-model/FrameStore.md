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
void pushFrame(src, size, w, h, linePitch, pixelFormat, timestamp);

bool getLatest(Frame& out) const;
bool getByWriteIndex(uint64_t idx, Frame& out) const;
bool getByWriteIndexROI(uint64_t idx, int roiX, int roiY, int roiW, int roiH,
                        Frame& out) const;  // avoids full-frame copy

bool saveFramesToDisk(dir, filterFn = nullptr) const;       // all frames (TIFF)
bool saveFramesToDisk(dir, startIdx, endIdx, filterFn);     // by index
bool saveFramesToDisk(dir, startTs, endTs, /*useTs=*/true, filterFn);

// Single-file uncompressed AVI export. fps only affects playback
// metadata, not the captured frame rate. Per-frame timestamps are
// NOT preserved in AVI.
bool saveFramesToAvi(path, fps = 30.0, filterFn = nullptr) const;
bool saveFramesToAvi(path, startIdx, endIdx, fps, filterFn);
bool saveFramesToAvi(path, startTs, endTs, /*useTs=*/true, fps, filterFn);

bool resize(size_t newCapacity);
size_t estimateMemoryBytesForCapacity(size_t capacity) const;
```

### AVI codec choice

`saveFramesToAvi` tries `fourcc('Y','8','0','0')` (single-channel Mono8)
first. If the backend can't open that codec it falls back to
`fourcc('D','I','B',' ')` (uncompressed BGR) and per-frame
`cv::cvtColor(GRAY2BGR)`. The fallback triples file size. Which path
actually runs depends on the platform's OpenCV backend (FFmpeg / VFW /
MSMF) — check the log line `"Writing AVI ... ({Y800/GRAY|DIB/BGR})"`.

Y800 is compact and bit-exact but **not playable in Windows Media
Player / Movies & TV** — those consumer decoders don't support 8-bit
grayscale AVIs and can crash trying. Confirmed to play in **VLC**, and
it round-trips cleanly through `cv::VideoCapture` and ImageJ, so mask
regeneration is unaffected. Use VLC (or ImageJ) to preview saved
buffers visually.

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

**Single-copy filter path:** when a filter is installed, `pushFrame` stages
the bytes once into a local `Frame`, runs the filter on it, and on accept
**moves** that buffer into the ring slot — so a filtered build pays one
full-frame copy, not two. The no-filter fast path is unchanged: it reuses the
slot's existing `data` capacity (`resize` + `copy_n`), so steady-state pushes
do not allocate. The whole operation runs under a single shared structural
lock so a concurrent `resize` cannot swap the buffers mid-push.

## Threading

Two-tier locking so the producer and consumers don't serialise on one lock
(the previous single-`std::mutex` design forced capture and every reader to
take turns *while holding the lock across the full-frame `memcpy`*, which
throttled both capture and the realtime pipeline — see
[[../current-state/Recent-Work]]):

- **`structureMutex_`** (`std::shared_mutex`) guards the *identity* of the
  ring (`ring_` / `slotMutexes_` / `capacity_`) and `frameFilter_`. The hot
  path (`pushFrame` / `getLatest` / `getByWriteIndex` / `getByWriteIndexROI`)
  takes it in **shared** mode; only whole-ring ops (`resize`, `saveFrames*`,
  `estimateMemoryBytesForCapacity`, `setFrameFilter`) take it **exclusive**.
- **`slotMutexes_`** — one `std::mutex` per ring slot. The actual frame copy
  in/out is done holding only that slot's lock, so the producer writing slot
  A never blocks a consumer reading slot B. Producer vs. consumer on the
  *same* slot (a wrap-around overwrite of the frame being read) is still
  serialised — correctness preserved.

`resize` rebuilds `slotMutexes_` alongside `ring_` under the exclusive lock;
this is safe because every hot-path op acquires the shared structural lock
*before* any slot lock, so no slot lock is held when `resize` runs.

## Gotchas

- If a consumer holds onto a `getByWriteIndex` copy for longer than
  `capacity / fps` seconds, its data is still valid (copy) but the index
  may fall off the available range.
- `pushFrame` copies bytes once (see "Single-copy filter path") — if you
  profile and see allocator pressure, investigate here first.
- `saveFramesToAvi` serialises frames while holding the mutex only long
  enough to snapshot the range; the VideoWriter loop runs outside the
  lock. Same pattern as `saveFramesToDisk`.
- With the empty-frame filter enabled, AVI frame count is smaller than
  the requested buffer range — 1:1 buffer-index-to-AVI-index mapping is
  lost. Acceptable for mask regen; don't depend on it elsewhere.
