#include "camera/mock/MockCamera.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>

namespace camera::mock
{

    namespace
    {
        constexpr uint64_t kPfncMono8 = 0x01080001;
        constexpr auto kMinFrameInterval = std::chrono::microseconds(1);

        bool hasSupportedExtension(const std::filesystem::path &path)
        {
            static const std::vector<std::string> exts = {
                ".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff"};
            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return std::find(exts.begin(), exts.end(), ext) != exts.end();
        }

    } // namespace

    bool isSupportedMockImageFile(const std::filesystem::path &path)
    {
        return hasSupportedExtension(path);
    }

    bool validateMockImageFolder(const std::filesystem::path &folder, std::string &errorMessage, bool requireAllReadable)
    {
        errorMessage.clear();
        if (!std::filesystem::exists(folder))
        {
            errorMessage = "Mock folder does not exist: " + folder.string();
            return false;
        }
        if (!std::filesystem::is_directory(folder))
        {
            errorMessage = "Mock path is not a directory: " + folder.string();
            return false;
        }

        std::vector<std::filesystem::path> files;
        for (const auto &entry : std::filesystem::directory_iterator(folder))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            if (!isSupportedMockImageFile(entry.path()))
            {
                continue;
            }
            files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        if (files.empty())
        {
            errorMessage = "No supported image files found in: " + folder.string();
            return false;
        }

        std::size_t readableCount = 0;
        for (const auto &path : files)
        {
            const std::string pathStr = path.string();
            cv::Mat img = cv::imread(pathStr, cv::IMREAD_GRAYSCALE);
            if (img.empty() || img.data == nullptr)
            {
                if (requireAllReadable)
                {
                    errorMessage = "Failed to read mock image file: " + path.string();
                    return false;
                }
                continue;
            }
            ++readableCount;
        }

        if (readableCount == 0)
        {
            errorMessage = "No readable image files found in: " + folder.string();
            return false;
        }
        return true;
    }

    MockCamera::MockCamera(MockCameraOptions options)
        : options_(std::move(options))
    {
        if (options_.frameInterval < kMinFrameInterval)
        {
            options_.frameInterval = kMinFrameInterval;
        }
    }

    void MockCamera::applyConfig(const camera::common::CameraConfig &config)
    {
        config_ = config;
    }

    bool MockCamera::start()
    {
        lastError_.clear();
        if (!validateMockImageFolder(options_.folder, lastError_, true))
        {
            SPDLOG_WARN("MockCamera: invalid folder configuration: {}", lastError_);
            running_ = false;
            return false;
        }
        refreshFileList();
        if (files_.empty())
        {
            SPDLOG_WARN("MockCamera: no images found in {}", options_.folder.string());
            running_ = false;
            return false;
        }

        // Preload all frames into memory for high-FPS playback
        preloadedFrames_.clear();
        if (!preloadFrames())
        {
            if (lastError_.empty())
            {
                SPDLOG_ERROR("MockCamera: failed to preload frames from {}", options_.folder.string());
            }
            else
            {
                SPDLOG_ERROR("MockCamera: failed to preload frames from {}: {}",
                             options_.folder.string(),
                             lastError_);
            }
            running_ = false;
            return false;
        }
        if (preloadedFrames_.empty())
        {
            SPDLOG_WARN("MockCamera: no valid frames could be preloaded from {}", options_.folder.string());
            running_ = false;
            return false;
        }

        nextIndex_ = 0;
        running_ = true;
        lastFrameTime_ = std::chrono::steady_clock::now() - options_.frameInterval;
        stats_ = {};

        SPDLOG_INFO("MockCamera started with {} files from {} (preloaded {} frames)",
                    files_.size(), options_.folder.string(), preloadedFrames_.size());
        return true;
    }

    void MockCamera::stop()
    {
        running_ = false;
        nextIndex_ = 0;
    }

    bool MockCamera::grabFrame(camera::common::Frame &out)
    {
        if (!running_)
        {
            return false;
        }

        if (preloadedFrames_.empty())
        {
            SPDLOG_WARN("MockCamera: no preloaded frames available to stream");
            running_ = false;
            return false;
        }

        const auto interval = options_.frameInterval;
        if (interval > std::chrono::microseconds::zero())
        {
            // Hybrid wait: coarse sleep for long intervals, busy-wait for sub-millisecond precision
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - lastFrameTime_;
            if (elapsed < interval)
            {
                auto remaining = interval - elapsed;
                // For >=2 ms, do a coarse sleep first to reduce CPU
                constexpr auto kCoarseThreshold = std::chrono::microseconds(2000);
                if (remaining >= kCoarseThreshold)
                {
                    // Leave ~1 ms for the fine wait to avoid oversleep on Windows
                    const auto coarse = remaining - std::chrono::milliseconds(1);
                    std::this_thread::sleep_for(coarse);
                }
                // Fine wait: busy-spin until target time
                const auto target = lastFrameTime_ + interval;
                do
                {
                    // Only yield when far from target; yielding too close can jitter
                    if ((target - std::chrono::steady_clock::now()) > std::chrono::microseconds(1000))
                    {
                        std::this_thread::yield();
                    }
                } while (std::chrono::steady_clock::now() < target);
            }
        }

        const camera::common::Frame &cached = preloadedFrames_[nextIndex_];

        const auto delivered = std::chrono::steady_clock::now();
        camera::common::Frame frameOut = cached;
        frameOut.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(delivered.time_since_epoch()).count());

        out = std::move(frameOut);

        nextIndex_ += 1;
        if (nextIndex_ >= preloadedFrames_.size())
        {
            if (options_.loopFiles)
            {
                nextIndex_ = 0;
            }
            else
            {
                running_ = false;
            }
        }

        const auto delta = delivered - lastFrameTime_;
        lastFrameTime_ = delivered;

        double fps = 0.0;
        const double seconds = std::chrono::duration<double>(delta).count();
        if (seconds > 0.0)
        {
            fps = 1.0 / seconds;
        }
        else if (interval > std::chrono::microseconds::zero())
        {
            fps = 1'000'000.0 / static_cast<double>(interval.count());
        }
        stats_.frameRate = fps > 0.0 ? static_cast<uint64_t>(std::llround(fps)) : 0;
        stats_.dataRateMBps = (fps > 0.0 && !out.data.empty())
                                  ? static_cast<uint64_t>(std::llround(
                                        (static_cast<double>(out.data.size()) * fps) / 1'000'000.0))
                                  : 0;

        return true;
    }

    bool MockCamera::pollStats(camera::common::CameraStats &out) const
    {
        if (!running_)
        {
            return false;
        }
        out = stats_;
        return true;
    }

    void MockCamera::setFrameInterval(std::chrono::microseconds interval)
    {
        options_.frameInterval = std::max(interval, kMinFrameInterval);
    }

    void MockCamera::setLooping(bool loop)
    {
        options_.loopFiles = loop;
    }

    void MockCamera::refreshFileList()
    {
        files_.clear();
        if (!std::filesystem::exists(options_.folder) || !std::filesystem::is_directory(options_.folder))
        {
            SPDLOG_WARN("MockCamera: folder {} does not exist or is not a directory", options_.folder.string());
            return;
        }

        for (const auto &entry : std::filesystem::directory_iterator(options_.folder))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            if (!isSupportedMockImageFile(entry.path()))
            {
                continue;
            }
            files_.push_back(entry.path());
        }

        std::sort(files_.begin(), files_.end());
    }

    bool MockCamera::loadFrameFromPath(const std::filesystem::path &path, camera::common::Frame &frame)
    {
        const std::string pathStr = path.string();
        cv::Mat img = cv::imread(pathStr, cv::IMREAD_GRAYSCALE);
        if (img.empty() || img.data == nullptr) {
            SPDLOG_WARN("MockCamera: OpenCV failed to load {}", path.string());
            return false;
        }

        frame.width = static_cast<uint64_t>(img.cols);
        frame.height = static_cast<uint64_t>(img.rows);
        frame.pixelFormat = kPfncMono8;

        const size_t width = static_cast<size_t>(img.cols);
        const size_t height = static_cast<size_t>(img.rows);

        if (img.isContinuous()) {
            frame.linePitch = width;
            const size_t dataSize = width * height;
            frame.data.resize(dataSize);
            std::memcpy(frame.data.data(), img.data, dataSize);
        } else {
            frame.linePitch = width;
            const size_t dataSize = width * height;
            frame.data.resize(dataSize);
            uint8_t* dst = frame.data.data();
            const uint8_t* src = img.data;
            for (int y = 0; y < img.rows; ++y) {
                std::memcpy(dst + y * width, src + y * img.step[0], width);
            }
        }

        SPDLOG_DEBUG("MockCamera: loaded {} using OpenCV", path.string());
        return true;
    }

    bool MockCamera::preloadFrames()
    {
        // Load and convert all images once to maximize grab throughput
        preloadedFrames_.clear();
        preloadedFrames_.reserve(files_.size());

        size_t loadedCount = 0;
        for (const auto &path : files_)
        {
            camera::common::Frame f;
            if (loadFrameFromPath(path, f))
            {
                preloadedFrames_.push_back(std::move(f));
                ++loadedCount;
            }
            else
            {
                lastError_ = "Failed to preload image file: " + path.string();
                return false;
            }
        }
        if (loadedCount != files_.size())
        {
            lastError_ = "Mock image preload count mismatch";
            return false;
        }
        return loadedCount > 0;
    }

} // namespace camera::mock
