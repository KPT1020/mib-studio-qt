// frame_store_bounds_test
//
// Regression test for a silent stale-frame read in FrameStore.
//
// getByWriteIndex / getByWriteIndexROI only rejected indices that are too NEW
// (writeIndex >= totalWritten). They had no lower bound, so for an index that
// has already been evicted from the ring (writeIndex < earliestAvailableIndex),
// `writeIndex % capacity` aliases to a live slot holding a much newer frame and
// the call returned true with the WRONG frame's contents — no error surfaced.
//
// This test overfills the ring deterministically (single-threaded) and asserts
// that evicted indices are rejected, in-window indices return the correct frame,
// and too-new indices are rejected. Each frame is filled with a unique uniform
// byte derived from its write index so a wrong (aliased) frame is detectable.

#include "backend/playback/FrameStore.h"

#include <cstdint>
#include <iostream>
#include <vector>

using backend::playback::FrameStore;
using backend::playback::Frame;

namespace {

constexpr uint64_t kW = 4;
constexpr uint64_t kH = 4;
constexpr size_t kBytes = static_cast<size_t>(kW * kH);
constexpr size_t kCapacity = 8;
constexpr uint64_t kPushed = 20;          // 2.5x capacity -> indices 0..11 evicted
constexpr uint64_t kPixelFormatMono8 = 0x01080001ULL;

uint8_t tagFor(uint64_t index) { return static_cast<uint8_t>(index % 251); }

bool allBytesEqual(const Frame& f, uint8_t v)
{
    if (f.data.size() < kBytes) return false;
    for (uint8_t b : f.data) {
        if (b != v) return false;
    }
    return true;
}

int failures = 0;

void expect(bool cond, const std::string& what)
{
    if (!cond) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

} // namespace

int main()
{
    FrameStore store(kCapacity);

    for (uint64_t i = 0; i < kPushed; ++i) {
        std::vector<uint8_t> buf(kBytes, tagFor(i));
        store.pushFrame(buf.data(), buf.size(), kW, kH,
                        /*linePitch=*/kW, kPixelFormatMono8, /*timestamp=*/i + 1);
    }

    const uint64_t earliest = store.earliestAvailableIndex(); // expect 12
    const uint64_t latest = store.latestAvailableIndex();     // expect 19
    expect(earliest == kPushed - kCapacity, "earliestAvailableIndex should be 12");
    expect(latest == kPushed - 1, "latestAvailableIndex should be 19");

    Frame f;

    // In-window reads return the CORRECT frame.
    expect(store.getByWriteIndex(latest, f) && allBytesEqual(f, tagFor(latest)),
           "getByWriteIndex(latest) returns the latest frame");
    expect(store.getByWriteIndex(earliest, f) && allBytesEqual(f, tagFor(earliest)),
           "getByWriteIndex(earliest) returns the earliest retained frame");

    // Too-new index rejected (existing behavior).
    expect(!store.getByWriteIndex(kPushed, f), "getByWriteIndex(totalWritten) rejected");

    // Evicted indices MUST be rejected, not silently aliased to a newer slot.
    // Without the lower-bound check, getByWriteIndex(5) returns true with the
    // contents of index 13 (5 % 8 == 13 % 8), and getByWriteIndex(0) returns
    // index 16's contents.
    expect(!store.getByWriteIndex(5, f), "evicted index 5 must be rejected");
    expect(!store.getByWriteIndex(0, f), "evicted index 0 must be rejected");
    expect(!store.getByWriteIndex(earliest - 1, f),
           "index just below the window must be rejected");

    // Same guarantees for the ROI accessor.
    Frame r;
    expect(store.getByWriteIndexROI(latest, 0, 0, 2, 2, r),
           "getByWriteIndexROI(latest) succeeds for an in-window index");
    expect(!store.getByWriteIndexROI(5, 0, 0, 2, 2, r),
           "getByWriteIndexROI must reject evicted index 5");
    expect(!store.getByWriteIndexROI(0, 0, 0, 2, 2, r),
           "getByWriteIndexROI must reject evicted index 0");

    if (failures == 0) {
        std::cout << "FrameStore bounds: evicted indices rejected, in-window reads correct\n";
        return 0;
    }
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
}
