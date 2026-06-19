// Verifies the config-generation + zero-copy background accessors (F3) that let
// the recording hot path cache config/ROI/background and refresh only on change
// instead of re-fetching (and cloning the background) every frame.
//
// Guards:
//  - getConfigGeneration() increments on setProcessingConfig / setRealtimeRoi /
//    setRealtimeBackgroundGray.
//  - getRealtimeBackgroundShared() returns null when unset, the stored buffer
//    (no clone) when set, is stable across calls, and tracks updates.

#include "backend/processing/ProcessingService.h"

#include <opencv2/core.hpp>

#include <iostream>

using backend::services::ProcessingConfig;
using backend::services::ProcessingService;

int main() {
    ProcessingService service;

    // No background set yet -> null.
    if (service.getRealtimeBackgroundShared() != nullptr) {
        std::cerr << "expected null background before any set\n";
        return 1;
    }

    const uint64_t g0 = service.getConfigGeneration();

    ProcessingConfig cfg;
    cfg.bg_subtract_threshold = 42;
    service.setProcessingConfig(cfg);
    const uint64_t g1 = service.getConfigGeneration();
    if (g1 <= g0) {
        std::cerr << "setProcessingConfig did not bump generation\n";
        return 2;
    }

    service.setRealtimeRoi(ProcessingService::Roi{1, 2, 3, 4});
    const uint64_t g2 = service.getConfigGeneration();
    if (g2 <= g1) {
        std::cerr << "setRealtimeRoi did not bump generation\n";
        return 3;
    }

    cv::Mat bg(16, 16, CV_8UC1, cv::Scalar(7));
    service.setRealtimeBackgroundGray(bg);
    const uint64_t g3 = service.getConfigGeneration();
    if (g3 <= g2) {
        std::cerr << "setRealtimeBackgroundGray did not bump generation\n";
        return 4;
    }

    // Shared accessor returns content-correct buffer, no clone per call.
    auto sharedA = service.getRealtimeBackgroundShared();
    auto sharedB = service.getRealtimeBackgroundShared();
    if (!sharedA || !sharedB) {
        std::cerr << "expected non-null background after set\n";
        return 5;
    }
    if (sharedA.get() != sharedB.get()) {
        std::cerr << "repeated calls should return the same buffer (no clone)\n";
        return 6;
    }
    if (sharedA->rows != 16 || sharedA->cols != 16 || sharedA->type() != CV_8UC1) {
        std::cerr << "background buffer has unexpected shape/type\n";
        return 7;
    }
    if (sharedA->at<uint8_t>(0, 0) != 7) {
        std::cerr << "background content mismatch\n";
        return 8;
    }

    // Updating the background swaps the buffer and bumps generation again.
    cv::Mat bg2(16, 16, CV_8UC1, cv::Scalar(9));
    service.setRealtimeBackgroundGray(bg2);
    const uint64_t g4 = service.getConfigGeneration();
    if (g4 <= g3) {
        std::cerr << "second setRealtimeBackgroundGray did not bump generation\n";
        return 9;
    }
    auto sharedC = service.getRealtimeBackgroundShared();
    if (!sharedC || sharedC.get() == sharedA.get()) {
        std::cerr << "expected a new background buffer after update\n";
        return 10;
    }
    if (sharedC->at<uint8_t>(0, 0) != 9) {
        std::cerr << "updated background content mismatch\n";
        return 11;
    }

    // Clearing the background returns null again.
    service.setRealtimeBackgroundGray(cv::Mat());
    if (service.getRealtimeBackgroundShared() != nullptr) {
        std::cerr << "expected null background after clear\n";
        return 12;
    }

    std::cout << "processing_config_generation_test OK\n";
    return 0;
}
