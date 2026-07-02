// frame_store_resize_growth_identity_test
//
// Regression test for a stale-frame aliasing bug in FrameStore::resize().
//
// The preserved-frame copy loop iterates the NEW preservation window
// [newEarliest, w), which for a GROWING resize (newCapacity > oldCapacity)
// after the ring has wrapped at least once extends further back in time than
// the OLD ring could ever have retained. It copied whatever Frame currently
// sat at `ring_[idx % oldCapacity]` and unconditionally labeled it with
// `newSlotIndices[idx % newCapacity] = idx`, without checking the old ring's
// own identity array (`slotWriteIndices_[idx % oldCapacity] == idx`). Because
// old-ring slots are physically shared by many absolute indices (idx,
// idx+oldCapacity, idx+2*oldCapacity, ...), for any idx below the true old
// retention floor (w - oldCapacity) the slot actually held a NEWER frame's
// data, which got silently relabeled with the OLDER idx. After the resize,
// getByWriteIndex(idx) for that stale idx would PASS the identity check
// (resize() itself had just written that identity) and return a
// self-consistent but WRONG frame.
//
// This reproduces single-threaded: push 20 frames into an 8-capacity store
// (ring wraps twice), grow-resize to 16, then query indices 4..7. Index 4
// no longer has ANY retained frame in an 8-capacity ring (only [12, 20) was
// ever live there), so it must be rejected -- not silently return frame 12's
// data relabeled as frame 4.

#include "backend/playback/FrameStore.h"

#include "support/assert.h"

#include <cstdint>
#include <vector>

using backend::playback::Frame;
using backend::playback::FrameStore;

namespace {

constexpr uint64_t kW = 4;
constexpr uint64_t kH = 4;
constexpr size_t kBytes = static_cast<size_t>(kW * kH);
constexpr size_t kOldCapacity = 8;
constexpr size_t kNewCapacity = 16;
constexpr uint64_t kPushed = 20; // 2.5x old capacity -> ring has wrapped twice
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

} // namespace

int main()
{
    FrameStore store(kOldCapacity);

    for (uint64_t i = 0; i < kPushed; ++i) {
        std::vector<uint8_t> buf(kBytes, tagFor(i));
        store.pushFrame(buf.data(), buf.size(), kW, kH,
                        /*linePitch=*/kW, kPixelFormatMono8, /*timestamp=*/i + 1);
    }

    // Before the resize, the true live window in the old (capacity-8) ring is
    // [12, 20). Indices 4..11 were already evicted and must never resurface.
    MIB_EXPECT(store.earliestAvailableIndex() == kPushed - kOldCapacity,
               "earliestAvailableIndex is 12 before resize");

    MIB_REQUIRE(store.resize(kNewCapacity), "grow-resize from 8 to 16 succeeds");

    Frame f;

    // Indices genuinely preserved from the old ring's live window must still
    // return their own, correct data.
    for (uint64_t idx = 12; idx < kPushed; ++idx) {
        MIB_EXPECT(store.getByWriteIndex(idx, f) && allBytesEqual(f, tagFor(idx)),
                   "index " + std::to_string(idx) + " (genuinely retained) returns its own frame");
    }

    // Indices 4..11 were NEVER retained by the capacity-8 ring and must be
    // rejected after the resize -- not aliased to a newer frame's data
    // (e.g. index 4 must not silently return frame 12's bytes).
    for (uint64_t idx = 4; idx < 12; ++idx) {
        Frame stale;
        const bool got = store.getByWriteIndex(idx, stale);
        MIB_EXPECT(!got, "index " + std::to_string(idx) +
                             " must be rejected: it was never retained by the 8-capacity ring");
        if (got) {
            MIB_EXPECT(!allBytesEqual(stale, tagFor(idx + kOldCapacity)),
                       "index " + std::to_string(idx) +
                           " must not silently alias a newer frame's data");
        }
    }

    // Indices below the new window's floor are still correctly rejected too.
    MIB_EXPECT(!store.getByWriteIndex(3, f), "index 3 (below new window) is rejected");

    return mib::test::exitCode();
}
