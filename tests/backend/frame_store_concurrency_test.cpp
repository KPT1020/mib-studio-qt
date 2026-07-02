// Stress test for FrameStore's two-tier locking (structureMutex_ + per-slot
// mutexes). The invariant under test: every byte of a frame returned by a
// reader equals (timestamp % 251), because the producer writes each frame as a
// single uniform value keyed off its index. A torn read (a consumer observing a
// slot mid-overwrite) would yield mixed byte values and fail. This guards the
// throttling fix that moved the full-frame copy out from under a single global
// mutex onto per-slot locks.

#include "backend/playback/FrameStore.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

using backend::playback::Frame;
using backend::playback::FrameStore;

namespace {

uint8_t expectedByte(uint64_t timestamp) {
    return static_cast<uint8_t>(timestamp % 251);
}

bool frameConsistent(const Frame& f) {
    if (f.data.empty()) {
        return true; // not yet populated; nothing to validate
    }
    if (f.data.size() != static_cast<size_t>(f.width * f.height)) {
        return false;
    }
    const uint8_t expected = expectedByte(f.timestamp);
    for (uint8_t b : f.data) {
        if (b != expected) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    constexpr size_t kCapacity = 64;
    constexpr uint64_t kTarget = 200000;

    FrameStore store(kCapacity);
    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        std::vector<uint8_t> buf;
        for (uint64_t i = 0; i < kTarget && !failed.load(); ++i) {
            const uint64_t w = 32;
            const uint64_t h = (i % 2 == 0) ? 16 : 24; // vary size to exercise reallocs
            const size_t size = static_cast<size_t>(w * h);
            buf.assign(size, expectedByte(i));
            store.pushFrame(buf.data(), size, w, h, /*linePitch=*/0,
                            /*pixelFormat=*/0x01080001, /*timestamp=*/i);
        }
        stop.store(true);
    });

    // Identity invariant (TOCTOU regression): if getByWriteIndex(idx, ...)
    // succeeds, the returned frame must BE frame idx (producer keys
    // timestamp == index). Before the under-slot-lock eviction re-check, a
    // wrapping producer could overwrite the slot between the eviction check
    // and the copy, returning a self-consistent but WRONG (newer) frame.
    auto identityOk = [&](const Frame& f, uint64_t idx) {
        return frameConsistent(f) && f.timestamp == idx;
    };

    auto consumer = [&] {
        Frame f;
        Frame g;
        while (!stop.load() && !failed.load()) {
            if (store.totalWritten() == 0) {
                continue;
            }
            const uint64_t latest = store.latestAvailableIndex();
            const uint64_t earliest = store.earliestAvailableIndex();

            if (store.getByWriteIndex(latest, f) && !identityOk(f, latest)) {
                failed.store(true);
                break;
            }
            if (earliest < latest &&
                store.getByWriteIndex((earliest + latest) / 2, f) &&
                !identityOk(f, (earliest + latest) / 2)) {
                failed.store(true);
                break;
            }
            // Hammer the eviction edge — the slot most likely to be
            // overwritten between the snapshot check and the slot lock.
            if (store.getByWriteIndex(earliest, f) && !identityOk(f, earliest)) {
                failed.store(true);
                break;
            }
            Frame roi;
            if (store.getByWriteIndexROI(earliest, 2, 2, 8, 8, roi) &&
                (roi.timestamp != earliest ||
                 (!roi.data.empty() && roi.data[0] != expectedByte(earliest)))) {
                failed.store(true);
                break;
            }
            if (store.getLatest(g) && !frameConsistent(g)) {
                failed.store(true);
                break;
            }
        }
    };

    std::vector<std::thread> consumers;
    for (int i = 0; i < 4; ++i) {
        consumers.emplace_back(consumer);
    }

    producer.join();
    for (auto& t : consumers) {
        t.join();
    }

    if (failed.load()) {
        std::cerr << "FrameStore: torn read / inconsistency detected under contention\n";
        return 1;
    }
    if (store.totalWritten() != kTarget) {
        std::cerr << "FrameStore: unexpected totalWritten " << store.totalWritten()
                  << " (expected " << kTarget << ")\n";
        return 2;
    }

    // Resize rebuilds the per-slot mutex array alongside the ring; verify reads
    // stay consistent across grow/shrink/grow.
    for (size_t cap : {static_cast<size_t>(128), static_cast<size_t>(32),
                       static_cast<size_t>(256)}) {
        if (!store.resize(cap)) {
            std::cerr << "FrameStore: resize to " << cap << " failed\n";
            return 3;
        }
        if (store.capacity() != cap) {
            std::cerr << "FrameStore: capacity mismatch after resize\n";
            return 4;
        }
        const uint64_t latest = store.latestAvailableIndex();
        Frame f;
        if (store.getByWriteIndex(latest, f) && !frameConsistent(f)) {
            std::cerr << "FrameStore: inconsistency after resize to " << cap << "\n";
            return 5;
        }
    }

    // Push again after the final resize to confirm slot locks track the new ring.
    std::vector<uint8_t> buf(32 * 16, expectedByte(999));
    store.pushFrame(buf.data(), buf.size(), 32, 16, 0, 0x01080001, 999);
    Frame f;
    if (!store.getLatest(f) || !frameConsistent(f)) {
        std::cerr << "FrameStore: post-resize push/read inconsistent\n";
        return 6;
    }

    std::cout << "FrameStore concurrency test OK\n";
    return 0;
}
