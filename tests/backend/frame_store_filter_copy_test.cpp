// Verifies FrameStore's filtered-push path (F1). When a frame filter is
// installed, pushFrame stages the bytes once for the filter and MOVES that
// buffer into the ring on accept (no second full-frame copy). This test guards
// the correctness of that move-on-accept path: accepted frames must be stored
// byte-identical with correct metadata, rejected frames must be dropped, and
// the no-filter fast path must remain intact.

#include "backend/playback/FrameStore.h"

#include <cstdint>
#include <iostream>
#include <vector>

using backend::playback::Frame;
using backend::playback::FrameStore;

namespace {

uint8_t valueFor(uint64_t i) { return static_cast<uint8_t>(i % 251); }

// Reject every third source frame.
bool rejected(uint64_t i) { return (i % 3) == 0; }

struct Expected {
    uint8_t value;
    uint64_t width;
    uint64_t height;
    uint64_t timestamp;
    size_t size;
};

bool checkFrame(const Frame& f, const Expected& e, int code, int& failCode) {
    if (f.width != e.width || f.height != e.height || f.timestamp != e.timestamp) {
        std::cerr << "metadata mismatch at code " << code << "\n";
        failCode = code;
        return false;
    }
    if (f.data.size() != e.size) {
        std::cerr << "size mismatch at code " << code << " (" << f.data.size()
                  << " vs " << e.size << ")\n";
        failCode = code;
        return false;
    }
    for (uint8_t b : f.data) {
        if (b != e.value) {
            std::cerr << "byte mismatch at code " << code << "\n";
            failCode = code;
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    constexpr size_t kCapacity = 256;
    FrameStore store(kCapacity);

    store.setFrameFilter([](const Frame& f) { return rejected(f.timestamp); });

    std::vector<Expected> accepted;
    uint64_t filteredExpected = 0;

    constexpr uint64_t kFrames = 100;
    std::vector<uint8_t> buf;
    for (uint64_t i = 0; i < kFrames; ++i) {
        const uint64_t w = 32;
        const uint64_t h = (i % 2 == 0) ? 16 : 24; // vary size to exercise reallocs
        const size_t size = static_cast<size_t>(w * h);
        buf.assign(size, valueFor(i));
        store.pushFrame(buf.data(), size, w, h, /*linePitch=*/0,
                        /*pixelFormat=*/0x01080001, /*timestamp=*/i);
        if (rejected(i)) {
            ++filteredExpected;
        } else {
            accepted.push_back(Expected{valueFor(i), w, h, i, size});
        }
    }

    if (store.totalWritten() != accepted.size()) {
        std::cerr << "totalWritten " << store.totalWritten() << " != accepted "
                  << accepted.size() << "\n";
        return 1;
    }
    if (store.totalFiltered() != filteredExpected) {
        std::cerr << "totalFiltered " << store.totalFiltered() << " != expected "
                  << filteredExpected << "\n";
        return 2;
    }

    // Every accepted frame must round-trip byte-identical (proves the moved
    // staged buffer carried the exact source bytes).
    int failCode = 0;
    for (size_t k = 0; k < accepted.size(); ++k) {
        Frame out;
        if (!store.getByWriteIndex(static_cast<uint64_t>(k), out)) {
            std::cerr << "getByWriteIndex failed at " << k << "\n";
            return 3;
        }
        if (!checkFrame(out, accepted[k], 4, failCode)) {
            return failCode;
        }
    }

    // No-filter fast path: clear the filter, push more, confirm they are all
    // stored and identical via getLatest.
    store.clearFrameFilter();
    const uint64_t writtenBefore = store.totalWritten();
    const uint64_t lastVal = 200;
    const uint64_t w = 32, h = 20;
    const size_t size = static_cast<size_t>(w * h);
    buf.assign(size, valueFor(lastVal));
    store.pushFrame(buf.data(), size, w, h, 0, 0x01080001, lastVal);

    if (store.totalWritten() != writtenBefore + 1) {
        std::cerr << "no-filter push did not increment totalWritten\n";
        return 5;
    }
    Frame latest;
    if (!store.getLatest(latest)) {
        std::cerr << "getLatest failed after no-filter push\n";
        return 6;
    }
    Expected lastExpected{valueFor(lastVal), w, h, lastVal, size};
    if (!checkFrame(latest, lastExpected, 7, failCode)) {
        return failCode;
    }

    std::cout << "frame_store_filter_copy_test OK: accepted=" << accepted.size()
              << " filtered=" << store.totalFiltered() << "\n";
    return 0;
}
