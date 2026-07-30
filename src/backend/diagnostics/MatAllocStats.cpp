#include "backend/diagnostics/MatAllocStats.h"

#include <opencv2/core.hpp>

#include <atomic>

namespace backend::diagnostics {

namespace {

std::atomic<uint64_t> allocCount_{0};
std::atomic<uint64_t> allocBytes_{0};

class CountingMatAllocator : public cv::MatAllocator {
public:
    explicit CountingMatAllocator(cv::MatAllocator* wrapped) : wrapped_(wrapped) {}

    cv::UMatData* allocate(int dims, const int* sizes, int type, void* data, size_t* step,
                           cv::AccessFlag flags, cv::UMatUsageFlags usageFlags) const override {
        cv::UMatData* u = wrapped_->allocate(dims, sizes, type, data, step, flags, usageFlags);
        // `data` non-null means the Mat wraps caller-owned memory — no heap
        // allocation happened, so only count allocator-owned buffers.
        if (u != nullptr && data == nullptr) {
            allocCount_.fetch_add(1, std::memory_order_relaxed);
            allocBytes_.fetch_add(u->size, std::memory_order_relaxed);
        }
        return u;
    }

    bool allocate(cv::UMatData* u, cv::AccessFlag accessFlags,
                  cv::UMatUsageFlags usageFlags) const override {
        return wrapped_->allocate(u, accessFlags, usageFlags);
    }

    void deallocate(cv::UMatData* u) const override { wrapped_->deallocate(u); }

private:
    cv::MatAllocator* wrapped_;
};

} // namespace

void MatAllocStats::install() {
    static bool installed = [] {
        cv::MatAllocator* def = cv::Mat::getDefaultAllocator();
        // Leaked singleton by design: Mats allocated through it may outlive
        // any scope, and OpenCV keeps a raw pointer.
        cv::Mat::setDefaultAllocator(new CountingMatAllocator(def));
        return true;
    }();
    (void)installed;
}

uint64_t MatAllocStats::allocCount() {
    return allocCount_.load(std::memory_order_relaxed);
}

uint64_t MatAllocStats::allocBytes() {
    return allocBytes_.load(std::memory_order_relaxed);
}

} // namespace backend::diagnostics
