#include "camera/mock/MockCamera.h"

#include <QImage>
#include <QImageReader>
#include <QString>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <optional>
#include <thread>
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace camera::mock
{

    namespace
    {
        constexpr uint64_t kPfncMono8 = 0x01080001;
        constexpr auto kSpinOnlyThreshold = std::chrono::microseconds(500);
        constexpr auto kCoarseGuard = std::chrono::microseconds(100);
        constexpr auto kSleepFineThreshold = std::chrono::microseconds(200);
        constexpr auto kYieldFineThreshold = std::chrono::microseconds(20);

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

        std::optional<int> parseOptionalCpuIndex(const char *value)
        {
            if (!value)
            {
                return std::nullopt;
            }

            try
            {
                const int parsed = std::stoi(value);
                if (parsed < 0)
                {
                    return std::nullopt;
                }
                return parsed;
            }
            catch (const std::exception &)
            {
                return std::nullopt;
            }
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

        configurePacingFromEnv();
        affinityAttempted_ = false;

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
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = now - lastFrameTime_;
            if (elapsed < interval)
            {
                const auto target = lastFrameTime_ + interval;
                applyThreadAffinityIfRequested();

                if (forceSpinPacing_ || interval <= spinOnlyThreshold_)
                {
                    // For sub-500 us pacing, pure spin avoids scheduler wakeup jitter.
                    while (std::chrono::steady_clock::now() < target)
                    {
                    }
                }
                else
                {
                    // For larger intervals, cooperate with scheduler first, then spin tail.
                    auto remaining = target - now;
                    if (remaining > kCoarseGuard)
                    {
                        const auto coarse = remaining - kCoarseGuard;
                        std::this_thread::sleep_for(coarse);
                        remaining = target - std::chrono::steady_clock::now();
                    }

                    while (std::chrono::steady_clock::now() < target)
                    {
                        const auto fineRemaining = target - std::chrono::steady_clock::now();
                        if (fineRemaining > kSleepFineThreshold)
                        {
                            std::this_thread::sleep_for(std::chrono::microseconds(50));
                        }
                        else if (fineRemaining > kYieldFineThreshold)
                        {
                            std::this_thread::yield();
                        }
                    }
                }
            }
        }

        const camera::common::Frame &cached = preloadedFrames_[nextIndex_];

        const auto delivered = std::chrono::steady_clock::now();
        out.width = cached.width;
        out.height = cached.height;
        out.linePitch = cached.linePitch;
        out.pixelFormat = cached.pixelFormat;
        if (out.data.size() != cached.data.size())
        {
            out.data.resize(cached.data.size());
        }
        if (!cached.data.empty())
        {
            std::memcpy(out.data.data(), cached.data.data(), cached.data.size());
        }
        out.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(delivered.time_since_epoch()).count());

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

        for (const auto &entry : std::filesystem::directory_iterator(options_.folder))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            if (!hasSupportedExtension(entry.path()))
            {
                continue;
            }
            files_.push_back(entry.path());
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
        // Load and convert all images once to maximize grab throughput.
        if (files_.empty())
        {
            preloadedFrames_.clear();
            return false;
        }

        const auto preloadStart = std::chrono::steady_clock::now();

        const size_t maxWorkers = std::max<size_t>(1, std::thread::hardware_concurrency());
        const size_t workerCount = std::min(files_.size(), maxWorkers);
        std::vector<std::optional<camera::common::Frame>> loaded(files_.size());
        std::atomic<size_t> nextFile{0};
        std::atomic<size_t> loadedCount{0};
        std::atomic<size_t> failedCount{0};

        auto worker = [&]()
        {
            for (;;)
            {
                const size_t i = nextFile.fetch_add(1, std::memory_order_relaxed);
                if (i >= files_.size())
                {
                    break;
                }

                camera::common::Frame frame;
                if (loadFrameFromPath(files_[i], frame))
                {
                    loaded[i] = std::move(frame);
                    loadedCount.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    failedCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        };

        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (size_t i = 0; i < workerCount; ++i)
        {
            workers.emplace_back(worker);
        }
        for (auto &thread : workers)
        {
            thread.join();
        }

        preloadedFrames_.clear();
        preloadedFrames_.reserve(loadedCount.load(std::memory_order_relaxed));
        for (size_t i = 0; i < loaded.size(); ++i)
        {
            if (loaded[i].has_value())
            {
                preloadedFrames_.push_back(std::move(*loaded[i]));
            }
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - preloadStart)
                                   .count();
        SPDLOG_INFO("MockCamera preload: loaded={} failed={} workers={} elapsed_ms={}",
                    preloadedFrames_.size(),
                    failedCount.load(std::memory_order_relaxed),
                    workerCount,
                    elapsedMs);

        if (preloadedFrames_.empty())
        {
            SPDLOG_WARN("MockCamera preload produced zero usable frames");
            return false;
        }
        return true;
    }

    void MockCamera::configurePacingFromEnv()
    {
        forceSpinPacing_ = false;
        spinOnlyThreshold_ = kSpinOnlyThreshold;
        pinnedCpu_ = -1;

        if (const char *envSpin = std::getenv("MIB_MOCK_CAMERA_FORCE_SPIN"))
        {
            std::string value = envSpin;
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            if (value == "1" || value == "true" || value == "yes" || value == "on")
            {
                forceSpinPacing_ = true;
            }
        }

        if (const char *envThreshold = std::getenv("MIB_MOCK_CAMERA_SPIN_THRESHOLD_US"))
        {
            try
            {
                const int parsed = std::stoi(envThreshold);
                if (parsed > 0)
                {
                    spinOnlyThreshold_ = std::chrono::microseconds(parsed);
                }
            }
            catch (const std::exception &)
            {
                SPDLOG_WARN("MockCamera: invalid MIB_MOCK_CAMERA_SPIN_THRESHOLD_US value: {}", envThreshold);
            }
        }

        if (const auto parsedCpuLegacy = parseOptionalCpuIndex(std::getenv("MIB_MOCK_CAMERA_PIN_CPU")); parsedCpuLegacy.has_value())
        {
            pinnedCpu_ = *parsedCpuLegacy;
        }

        SPDLOG_INFO("MockCamera pacing config: spin_only_threshold_us={} force_spin={} pin_cpu={}",
                    spinOnlyThreshold_.count(),
                    forceSpinPacing_ ? 1 : 0,
                    pinnedCpu_);
    }

    void MockCamera::applyThreadAffinityIfRequested()
    {
        if (affinityAttempted_)
        {
            return;
        }
        affinityAttempted_ = true;

        if (pinnedCpu_ < 0)
        {
            return;
        }

#ifdef __linux__
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(pinnedCpu_, &set);
        const int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &set);
        if (rc != 0)
        {
            SPDLOG_WARN("MockCamera: failed to pin capture thread to cpu {} (errno={})", pinnedCpu_, rc);
            return;
        }
        SPDLOG_INFO("MockCamera: capture thread pinned to cpu {}", pinnedCpu_);
#else
        SPDLOG_WARN("MockCamera: MIB_MOCK_CAMERA_PIN_CPU is set but CPU affinity is only supported on Linux in this build");
#endif
    }

} // namespace camera::mock
