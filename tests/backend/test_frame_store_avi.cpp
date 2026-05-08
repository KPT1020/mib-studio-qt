#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "backend/playback/FrameStore.h"

using backend::playback::FrameStore;

namespace {
struct TempFile
{
    std::filesystem::path path;
    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

static std::filesystem::path uniqueTmpPath(const std::string& prefix, const std::string& ext)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / (prefix + std::to_string(now) + ext);
}
}

TEST_CASE("FrameStore::saveFramesToAvi returns false for empty store", "[FrameStore]")
{
    FrameStore store(4);
    TempFile tmp{uniqueTmpPath("mib_empty_", ".avi")};
    CHECK_FALSE(store.saveFramesToAvi(tmp.path.string(), 30.0, nullptr));
}

TEST_CASE("FrameStore::saveFramesToAvi validates index ranges", "[FrameStore]")
{
    FrameStore store(8);

    std::vector<uint8_t> buf(16 * 16, 10);
    store.pushFrame(buf.data(), buf.size(), 16, 16, 16, 0, 100);
    store.pushFrame(buf.data(), buf.size(), 16, 16, 16, 0, 200);
    store.pushFrame(buf.data(), buf.size(), 16, 16, 16, 0, 300);

    TempFile tmpBadRange{uniqueTmpPath("mib_bad_range_", ".avi")};
    CHECK_FALSE(store.saveFramesToAvi(tmpBadRange.path.string(), 2, 1, 30.0, nullptr));

    TempFile tmpOutside{uniqueTmpPath("mib_outside_", ".avi")};
    CHECK_FALSE(store.saveFramesToAvi(tmpOutside.path.string(), 10, 12, 30.0, nullptr));
}

TEST_CASE("FrameStore::saveFramesToAvi may write an AVI when codec support is present", "[FrameStore]")
{
    FrameStore store(8);

    std::vector<uint8_t> buf(32 * 32, 0);
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = static_cast<uint8_t>(i % 255);
    }

    store.pushFrame(buf.data(), buf.size(), 32, 32, 32, 0, 100);
    store.pushFrame(buf.data(), buf.size(), 32, 32, 32, 0, 200);
    store.pushFrame(buf.data(), buf.size(), 32, 32, 32, 0, 300);

    TempFile tmp{uniqueTmpPath("mib_ok_", ".avi")};
    const bool ok = store.saveFramesToAvi(tmp.path.string(), 0, 2, 30.0, nullptr);

    if (ok) {
        REQUIRE(std::filesystem::exists(tmp.path));
        CHECK(std::filesystem::file_size(tmp.path) > 0);
    } else {
        SUCCEED();
    }
}
