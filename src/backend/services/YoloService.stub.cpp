#include "backend/services/YoloService.h"

#include <spdlog/spdlog.h>

namespace Ort {
class Env {};
class Session {};
}  // namespace Ort

namespace backend::services {

namespace {
bool& warnedUnavailable() {
    static bool warned = false;
    return warned;
}

void logUnavailableOnce() {
    if (!warnedUnavailable()) {
        warnedUnavailable() = true;
        SPDLOG_WARN("YoloService: ONNX Runtime is unavailable; YOLO features are disabled in this build");
    }
}
}  // namespace

YoloService::YoloService() = default;

YoloService::~YoloService() = default;

std::string YoloService::resolveModelPath(const std::string& basePath) {
    return basePath;
}

bool YoloService::initialize(const std::string& /*modelPath*/) {
    logUnavailableOnce();
    isLoaded_ = false;
    return false;
}

Ort::Session* YoloService::getSession() const {
    return nullptr;
}

Ort::Env* YoloService::getEnv() const {
    return nullptr;
}

}  // namespace backend::services
