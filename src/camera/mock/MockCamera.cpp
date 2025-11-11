#include "camera/mock/MockCamera.h"

#include <QImage>
#include <QImageReader>
#include <QString>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <thread>

namespace camera::mock {

namespace {
constexpr uint64_t kPfncMono8 = 0x01080001;

bool hasSupportedExtension(const std::filesystem::path& path) {
    static const std::vector<std::string> exts = {
        ".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff"
    };
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

QString toQString(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromUtf8(path.u8string().c_str());
#endif
}
} // namespace

MockCamera::MockCamera(MockCameraOptions options)
    : options_(std::move(options)) {}

void MockCamera::applyConfig(const camera::common::CameraConfig& config) {
    config_ = config;
}

bool MockCamera::start() {
    refreshFileList();
    if (files_.empty()) {
        SPDLOG_WARN("MockCamera: no images found in {}", options_.folder.string());
        running_ = false;
        return false;
    }

    nextIndex_ = 0;
    running_ = true;
    lastFrameTime_ = std::chrono::steady_clock::now();
    stats_ = {};

    SPDLOG_INFO("MockCamera started with {} files from {}", files_.size(), options_.folder.string());
    return true;
}

void MockCamera::stop() {
    running_ = false;
    nextIndex_ = 0;
}

bool MockCamera::grabFrame(camera::common::Frame& out) {
    if (!running_) {
        return false;
    }

    if (files_.empty()) {
        SPDLOG_WARN("MockCamera: no files available to stream");
        running_ = false;
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    if (options_.frameInterval.count() > 0) {
        const auto elapsed = now - lastFrameTime_;
        if (elapsed < options_.frameInterval) {
            std::this_thread::sleep_for(options_.frameInterval - elapsed);
        }
    }

    camera::common::Frame frame;
    size_t attempts = files_.size();
    bool loaded = false;
    while (attempts-- > 0 && !loaded) {
        const auto& path = files_[nextIndex_];
        if (!loadFrameFromPath(path, frame)) {
            SPDLOG_WARN("MockCamera: failed to load {}, skipping", path.string());
            nextIndex_ = (nextIndex_ + 1) % files_.size();
            continue;
        }
        loaded = true;
    }

    if (!loaded) {
        SPDLOG_ERROR("MockCamera: unable to load any frames from {}", options_.folder.string());
        running_ = false;
        return false;
    }

    const auto delivered = std::chrono::steady_clock::now();
    frame.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(delivered.time_since_epoch()).count());

    out = std::move(frame);

    nextIndex_ += 1;
    if (nextIndex_ >= files_.size()) {
        if (options_.loopFiles) {
            nextIndex_ = 0;
        } else {
            running_ = false;
        }
    }

    const auto delta = delivered - lastFrameTime_;
    lastFrameTime_ = delivered;

    double fps = 0.0;
    if (options_.frameInterval.count() > 0) {
        fps = 1000.0 / static_cast<double>(options_.frameInterval.count());
    } else {
        const double seconds = std::chrono::duration<double>(delta).count();
        if (seconds > 0.0) {
            fps = 1.0 / seconds;
        }
    }
    stats_.frameRate = fps > 0.0 ? static_cast<uint64_t>(std::llround(fps)) : 0;
    stats_.dataRateMBps = (fps > 0.0 && !out.data.empty())
                              ? static_cast<uint64_t>(std::llround(
                                    (static_cast<double>(out.data.size()) * fps) / 1'000'000.0))
                              : 0;

    return true;
}

bool MockCamera::pollStats(camera::common::CameraStats& out) const {
    if (!running_) {
        return false;
    }
    out = stats_;
    return true;
}

void MockCamera::setFrameInterval(std::chrono::milliseconds interval) {
    options_.frameInterval = interval;
}

void MockCamera::setLooping(bool loop) {
    options_.loopFiles = loop;
}

void MockCamera::refreshFileList() {
    files_.clear();
    if (!std::filesystem::exists(options_.folder) || !std::filesystem::is_directory(options_.folder)) {
        SPDLOG_WARN("MockCamera: folder {} does not exist or is not a directory", options_.folder.string());
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(options_.folder)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!hasSupportedExtension(entry.path())) {
            continue;
        }
        files_.push_back(entry.path());
    }

    std::sort(files_.begin(), files_.end());
}

bool MockCamera::loadFrameFromPath(const std::filesystem::path& path, camera::common::Frame& frame) {
    QImageReader reader(toQString(path));
    reader.setAutoTransform(true);

    QImage image = reader.read();
    if (image.isNull()) {
        SPDLOG_WARN("MockCamera: QImageReader failed for {} ({})", path.string(), reader.errorString().toStdString());
        return false;
    }

    QImage mono = image.convertToFormat(QImage::Format_Grayscale8);
    frame.width = static_cast<uint64_t>(mono.width());
    frame.height = static_cast<uint64_t>(mono.height());
    frame.linePitch = static_cast<size_t>(mono.bytesPerLine());
    frame.pixelFormat = kPfncMono8;
    frame.data.resize(static_cast<size_t>(mono.sizeInBytes()));
    if (!frame.data.empty()) {
        std::memcpy(frame.data.data(), mono.constBits(), frame.data.size());
    }

    return true;
}

} // namespace camera::mock


