#include "backend/camera/mock/MockCamera.h"

#include <QImage>
#include <QImageReader>
#include <QString>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <thread>

namespace camera::mock
{

    namespace
    {
        constexpr uint64_t kPfncMono8 = 0x01080001;

        bool hasSupportedExtension(const std::filesystem::path &path)
        {
            static const std::vector<std::string> exts = {
                ".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff"};
            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return std::find(exts.begin(), exts.end(), ext) != exts.end();
        }

        QString toQString(const std::filesystem::path &path)
        {
#ifdef _WIN32
            return QString::fromStdWString(path.wstring());
#else
            return QString::fromUtf8(path.u8string().c_str());
#endif
        }
    } // namespace

    MockCamera::MockCamera(MockCameraOptions options)
        : options_(std::move(options)) {}

    void MockCamera::applyConfig(const camera::common::CameraConfig &config)
    {
        config_ = config;
    }

    bool MockCamera::start()
    {
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
            SPDLOG_ERROR("MockCamera: failed to preload frames from {}", options_.folder.string());
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
        lastFrameTime_ = std::chrono::steady_clock::now();
        stats_ = {};

        SPDLOG_INFO("MockCamera started with {} files from {} (preloaded {} frames)",
                    files_.size(), options_.folder.string(), preloadedFrames_.size());
        return true;
    }

    void MockCamera::stop()
    {
        // Only flip the (atomic) running_ flag. The capture thread observes it in
        // grabFrame()/isRunning() and then stops touching the other members;
        // start() resets nextIndex_ before the next capture thread runs.
        // Resetting nextIndex_ here would race with the still-draining capture
        // thread (flagged by ThreadSanitizer).
        running_.store(false, std::memory_order_release);
    }

    bool MockCamera::grabFrame(camera::common::Frame &out)
    {
        if (!running_.load(std::memory_order_acquire))
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
        if (!running_.load(std::memory_order_acquire))
        {
            return false;
        }
        out = stats_;
        return true;
    }

    void MockCamera::setFrameInterval(std::chrono::microseconds interval)
    {
        options_.frameInterval = interval;
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

        // error_code overload: the folder can vanish between the exists()
        // check above and iteration; the throwing overload would propagate
        // std::filesystem_error out of start().
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(options_.folder, ec))
        {
            std::error_code entryEc;
            if (!entry.is_regular_file(entryEc) || entryEc)
            {
                continue;
            }
            if (!hasSupportedExtension(entry.path()))
            {
                continue;
            }
            files_.push_back(entry.path());
        }
        if (ec)
        {
            SPDLOG_WARN("MockCamera: failed to enumerate {}: {}", options_.folder.string(), ec.message());
        }

        std::sort(files_.begin(), files_.end());
    }

    bool MockCamera::loadFrameFromPath(const std::filesystem::path &path, camera::common::Frame &frame)
    {
        // Try QImageReader first (works for most formats)
        QImageReader reader(toQString(path));
        reader.setAutoTransform(true);

        QImage image = reader.read();
        if (!image.isNull())
        {
            // QImageReader succeeded
            QImage mono = image.convertToFormat(QImage::Format_Grayscale8);
            frame.width = static_cast<uint64_t>(mono.width());
            frame.height = static_cast<uint64_t>(mono.height());
            frame.linePitch = static_cast<size_t>(mono.bytesPerLine());
            frame.pixelFormat = kPfncMono8;
            frame.data.resize(static_cast<size_t>(mono.sizeInBytes()));
            if (!frame.data.empty())
            {
                std::memcpy(frame.data.data(), mono.constBits(), frame.data.size());
            }
            SPDLOG_DEBUG("MockCamera: loaded {} using QImageReader", path.string());
            return true;
        }

        // QImageReader failed - check if this is a TIFF file and try OpenCV fallback
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        
        bool isTiff = (ext == ".tif" || ext == ".tiff");
        
        if (isTiff)
        {
            // Try OpenCV as fallback for TIFF files
            std::string pathStr = path.string();
            cv::Mat img = cv::imread(pathStr, cv::IMREAD_GRAYSCALE);
            
            if (!img.empty() && img.data != nullptr)
            {
                // OpenCV succeeded - convert cv::Mat to Frame format
                frame.width = static_cast<uint64_t>(img.cols);
                frame.height = static_cast<uint64_t>(img.rows);
                frame.pixelFormat = kPfncMono8;
                
                // Calculate data size and line pitch
                // For Frame format, linePitch should match the actual data stride we're storing
                const size_t width = static_cast<size_t>(img.cols);
                const size_t height = static_cast<size_t>(img.rows);
                
                if (img.isContinuous())
                {
                    // Data is contiguous - linePitch equals width
                    frame.linePitch = width;
                    size_t dataSize = width * height;
                    frame.data.resize(dataSize);
                    std::memcpy(frame.data.data(), img.data, dataSize);
                }
                else
                {
                    // Data has padding - copy row by row, linePitch equals width (no padding in Frame)
                    frame.linePitch = width;
                    size_t dataSize = width * height;
                    frame.data.resize(dataSize);
                    
                    uint8_t* dst = frame.data.data();
                    const uint8_t* src = img.data;
                    for (int y = 0; y < img.rows; ++y)
                    {
                        std::memcpy(dst + y * width, src + y * img.step[0], width);
                    }
                }
                
                SPDLOG_DEBUG("MockCamera: loaded {} using OpenCV (QImageReader fallback)", path.string());
                return true;
            }
            else
            {
                SPDLOG_WARN("MockCamera: both QImageReader and OpenCV failed for {} (QImageReader: {})", 
                           path.string(), reader.errorString().toStdString());
                return false;
            }
        }
        else
        {
            // Not a TIFF file, QImageReader failure is final
            SPDLOG_WARN("MockCamera: QImageReader failed for {} ({})", path.string(), reader.errorString().toStdString());
            return false;
        }
    }

    bool MockCamera::preloadFrames()
    {
        // Load and convert all images once to maximize grab throughput
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
                // Warning already emitted by loadFrameFromPath; continue
            }
        }
        return loadedCount > 0;
    }

} // namespace camera::mock
