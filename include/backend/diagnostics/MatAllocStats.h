#pragma once

#include <cstdint>

namespace backend::diagnostics {

// Process-wide cv::Mat allocation accounting: a delegating cv::MatAllocator
// that counts every allocation and its byte size (two relaxed atomic adds
// per alloc) before forwarding to OpenCV's default allocator. This is the
// direct measurement of per-frame heap churn — cv::Mat temporaries in the
// realtime loop, per-frame clones, batch copies — which RSS alone cannot
// attribute. Cumulative monotonic counters; PipelineTrendSampler samples
// them at 1 Hz and the analyzer derives allocation rates.
class MatAllocStats {
public:
    // Install the counting allocator (idempotent). Call once at startup,
    // before pipeline threads allocate — AppBackend::initialize does.
    static void install();

    static uint64_t allocCount();
    static uint64_t allocBytes();
};

} // namespace backend::diagnostics
