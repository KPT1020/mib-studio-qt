// frame_store_reserve_test
//
// reserveFrameBytes pre-sizes ring slots so the pushFrame hot path is
// allocation-free for frames of that size or smaller. Verifies reservation +
// round-trip correctness and that oversize frames still grow correctly.

#include "backend/playback/FrameStore.h"

#include "support/assert.h"

#include <cstdint>
#include <vector>

using backend::playback::FrameStore;
using backend::playback::Frame;

int main()
{
    const size_t cap = 8;
    const size_t bytes = 64 * 64; // 4096

    FrameStore fs(cap);
    fs.reserveFrameBytes(bytes);

    std::vector<uint8_t> src(bytes, 7);
    // Push two full rings worth; data of size == reserved must round-trip and
    // the ring caps at capacity.
    for (size_t i = 0; i < cap * 2; ++i) {
        fs.pushFrame(src.data(), src.size(), 64, 64, 64, 0x01080001u, i + 1);
    }
    MIB_REQUIRE(fs.totalWritten() == cap * 2, "all frames pushed");
    MIB_REQUIRE(fs.availableCount() == cap, "ring capped at capacity");

    Frame out;
    MIB_REQUIRE(fs.getByWriteIndex(cap * 2 - 1, out), "latest frame retrievable");
    MIB_EXPECT(out.data.size() == bytes && out.data[0] == 7, "frame data intact");

    // A larger-than-reserved frame must still work (grows that slot).
    std::vector<uint8_t> big(bytes * 2, 9);
    fs.pushFrame(big.data(), big.size(), 128, 64, 128, 0x01080001u, 999);
    MIB_REQUIRE(fs.getByWriteIndex(cap * 2, out), "oversize frame retrievable");
    MIB_EXPECT(out.data.size() == bytes * 2 && out.data[0] == 9, "oversize frame intact");

    // Reserving again with a smaller value is a no-op (idempotent, only grows).
    fs.reserveFrameBytes(16);
    fs.pushFrame(src.data(), src.size(), 64, 64, 64, 0x01080001u, 1000);
    MIB_REQUIRE(fs.getByWriteIndex(cap * 2 + 1, out), "post-reserve push retrievable");
    MIB_EXPECT(out.data.size() == bytes, "smaller reserve did not shrink behavior");

    if (mib::test::exitCode() == 0) {
        std::printf("FrameStore reserveFrameBytes verified\n");
    }
    return mib::test::exitCode();
}
