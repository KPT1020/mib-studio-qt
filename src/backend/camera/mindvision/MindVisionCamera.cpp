#include "backend/camera/mindvision/MindVisionCamera.h"

#ifndef MIB_HAS_MINDVISION
#define MIB_HAS_MINDVISION 0
#endif

#if MIB_HAS_MINDVISION

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdio.h>
#endif

#define API_LOAD_MAIN
#if __has_include(<MindVision/CameraApiLoad.h>)
#include <MindVision/CameraApiLoad.h>
#elif __has_include(<CameraApiLoad.h>)
#include <CameraApiLoad.h>
#else
#error "MindVision CameraApiLoad.h not found"
#endif

// Compile-time detection of the newest-frame priority retrieval API
// (CameraGetImageBufferPriority + CAMERA_GET_IMAGE_PRIORITY_NEWEST).
// SDK variants that expose the priority constant as a preprocessor #define are
// detected automatically. Variants that declare emCameraGetImagePriority as a
// plain C enum in CameraDefine.h (every public SDK mirror observed as of
// 2026-08) are invisible to the preprocessor, so those builds take the
// bounded-drain fallback below unless the build opts in explicitly with
// -DMIB_MINDVISION_USE_PRIORITY_API=1 after verifying the SDK ships the API.
#ifndef MIB_MINDVISION_USE_PRIORITY_API
#define MIB_MINDVISION_USE_PRIORITY_API 0
#endif
#if defined(CAMERA_GET_IMAGE_PRIORITY_NEWEST) || MIB_MINDVISION_USE_PRIORITY_API
#define MIB_MINDVISION_HAS_PRIORITY_NEWEST 1
#else
#define MIB_MINDVISION_HAS_PRIORITY_NEWEST 0
#endif

#include "backend/camera/mindvision/MindVisionApply.h"
#include "backend/camera/mindvision/MindVisionConfig.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>
#include <string>

namespace camera::common
{

namespace
{
    constexpr std::uint64_t kMono8PfncCode = 0x01080001u;
}

MindVisionCamera::MindVisionCamera(int cameraIndex, std::string configPath)
    : cameraIndex_(cameraIndex), configPath_(std::move(configPath))
{
}

MindVisionCamera::~MindVisionCamera()
{
    stop();
}

void MindVisionCamera::applyConfig(const CameraConfig &config)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    config_ = config;
}

bool MindVisionCamera::applyJsonConfig(int hCamera)
{
    if (configPath_.empty())
    {
        return true;
    }

    QFile file(QString::fromStdString(configPath_));
    if (!file.open(QIODevice::ReadOnly))
    {
        SPDLOG_WARN("MindVisionCamera: cannot open config file {}", configPath_);
        return false;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    const auto parsed = backend::camera::mindvision::parseConfig(bytes);
    if (!parsed.ok)
    {
        SPDLOG_WARN("MindVisionCamera: {} in {}", parsed.error, configPath_);
        return false;
    }
    for (const auto& warning : parsed.warnings)
    {
        SPDLOG_WARN("{} (in {})", warning, configPath_);
    }

    configuredTriggerMode_ = parsed.config.triggerMode;
    return backend::camera::mindvision::applyConfigToHandle(hCamera, parsed.config, nullptr);
}

bool MindVisionCamera::start()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (running_)
    {
        return true;
    }

    if (LoadSdkApi() != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_ERROR("MindVisionCamera: failed to load MVCAMSDK DLL");
        return false;
    }

    CameraSdkStatus status = CameraSdkInit(0);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSdkInit returned {}", status);
    }

    tSdkCameraDevInfo devList[32];
    INT count = 32;
    status = CameraEnumerateDevice(devList, &count);
    if (status != CAMERA_STATUS_SUCCESS || count <= 0)
    {
        SPDLOG_ERROR("MindVisionCamera: CameraEnumerateDevice failed (status={}, count={})", status, count);
        return false;
    }

    if (cameraIndex_ < 0 || cameraIndex_ >= count)
    {
        SPDLOG_ERROR("MindVisionCamera: cameraIndex={} out of range (found {})", cameraIndex_, count);
        return false;
    }

    CameraHandle hCamera = -1;
    status = CameraInit(&devList[cameraIndex_], -1, -1, &hCamera);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_ERROR("MindVisionCamera: CameraInit failed (status={})", status);
        return false;
    }
    hCamera_ = hCamera;

    tSdkCameraCapbility cap{};
    status = CameraGetCapability(hCamera_, &cap);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_ERROR("MindVisionCamera: CameraGetCapability failed (status={})", status);
        CameraUnInit(hCamera_);
        hCamera_ = -1;
        return false;
    }

    if (!applyJsonConfig(hCamera_))
    {
        CameraUnInit(hCamera_);
        hCamera_ = -1;
        return false;
    }

    tSdkImageResolution res{};
    status = CameraGetImageResolution(hCamera_, &res);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_ERROR("MindVisionCamera: CameraGetImageResolution failed (status={})", status);
        CameraUnInit(hCamera_);
        hCamera_ = -1;
        return false;
    }

    bufferWidth_ = res.iWidth;
    bufferHeight_ = res.iHeight;

    // Unconditional: outBuffer_ is sized 1 byte/px and the pipeline is
    // mono8-only, so a color sensor left at the ISP's 3-byte default would
    // overrun the buffer in CameraImageProcess. The ISP converts color to
    // mono8 when asked.
    status = CameraSetIspOutFormat(hCamera_, CAMERA_MEDIA_TYPE_MONO8);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetIspOutFormat(MONO8) returned {}", status);
    }

    const int bufferSize = bufferWidth_ * bufferHeight_;
    outBuffer_ = CameraAlignMalloc(bufferSize, 16);
    if (!outBuffer_)
    {
        SPDLOG_ERROR("MindVisionCamera: CameraAlignMalloc({}) failed", bufferSize);
        CameraUnInit(hCamera_);
        hCamera_ = -1;
        return false;
    }

    status = CameraPlay(hCamera_);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_ERROR("MindVisionCamera: CameraPlay failed (status={})", status);
        CameraAlignFree(outBuffer_);
        outBuffer_ = nullptr;
        CameraUnInit(hCamera_);
        hCamera_ = -1;
        return false;
    }

    frameCount_ = 0;
    intentionalDiscards_.store(0, std::memory_order_relaxed);
    startTime_ = std::chrono::steady_clock::now();

    // Confirm the delivery mode for this run. Mode changes require a full
    // stop() -> applyConfig() -> start() cycle (modeChangeRequiresRestart);
    // we never clear or reorder an active SDK queue mid-run.
    confirmedDeliveryMode_ = config_.deliveryMode;
    deliveryModeConfirmed_ = true;

    running_ = true;
    SPDLOG_INFO("MindVisionCamera: started (index={}, {}x{}, mono={}, deliveryMode={})",
                cameraIndex_, bufferWidth_, bufferHeight_, static_cast<int>(cap.sIspCapacity.bMonoSensor),
                toString(confirmedDeliveryMode_));
    return true;
}

void MindVisionCamera::stop()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_)
    {
        return;
    }
    running_ = false;

    if (hCamera_ >= 0)
    {
        CameraStop(hCamera_);
        if (outBuffer_)
        {
            CameraAlignFree(outBuffer_);
            outBuffer_ = nullptr;
        }
        CameraUnInit(hCamera_);
        hCamera_ = -1;
    }

    SPDLOG_INFO("MindVisionCamera: stopped");
}

bool MindVisionCamera::grabFrame(Frame &out)
{
    while (true)
    {
        int hCamera = -1;
        FrameDeliveryMode mode = FrameDeliveryMode::EveryFrame;
        int maxDrain = 0;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (!running_)
            {
                return false;
            }
            hCamera = hCamera_;
            mode = confirmedDeliveryMode_;
            maxDrain = config_.numBuffers;
        }

        tSdkFrameHead frameHead{};
        BYTE *pBuffer = nullptr;
        CameraSdkStatus status;

        if (mode == FrameDeliveryMode::LatestFrame)
        {
#if MIB_MINDVISION_HAS_PRIORITY_NEWEST
            (void)maxDrain; // Only the drain fallback bounds its loop with it.
            // SDK-side newest-frame retrieval: the SDK discards every completed
            // buffer older than the returned one before handing it out.
            // Limitation: those SDK-internal skips have no per-skip callback,
            // so intentionalDiscards_ cannot count them exactly on this path
            // (the drain fallback below does count exactly).
            status = CameraGetImageBufferPriority(hCamera, &frameHead, &pBuffer, 100,
                                                  CAMERA_GET_IMAGE_PRIORITY_NEWEST);
#else
            // Fallback for SDK variants without the priority API: block for the
            // next completed buffer, then drain any further already-completed
            // buffers non-blocking (zero timeout), keeping only the newest.
            // Bounded by numBuffers so a producer outpacing us cannot starve
            // delivery. Exactly one buffer is held at any time; each superseded
            // buffer is released immediately and counted as an intentional
            // discard.
            status = CameraGetImageBuffer(hCamera, &frameHead, &pBuffer, 100);
            if (status == CAMERA_STATUS_SUCCESS)
            {
                for (int i = 0; i < maxDrain; ++i)
                {
                    tSdkFrameHead newerHead{};
                    BYTE *newerBuffer = nullptr;
                    const CameraSdkStatus drainStatus =
                        CameraGetImageBuffer(hCamera, &newerHead, &newerBuffer, 0);
                    if (drainStatus != CAMERA_STATUS_SUCCESS)
                    {
                        break; // No newer completed buffer; keep the held one.
                    }
                    CameraReleaseImageBuffer(hCamera, pBuffer);
                    intentionalDiscards_.fetch_add(1, std::memory_order_relaxed);
                    pBuffer = newerBuffer;
                    frameHead = newerHead;
                }
            }
#endif
        }
        else
        {
            // EveryFrame: ordered retrieval, never skip. The priority API is
            // deliberately not used here.
            status = CameraGetImageBuffer(hCamera, &frameHead, &pBuffer, 100);
        }

        if (status == CAMERA_STATUS_TIME_OUT)
        {
            continue;
        }
        if (status != CAMERA_STATUS_SUCCESS)
        {
            SPDLOG_WARN("MindVisionCamera: frame retrieval returned {} (mode={})",
                        status, toString(mode));
            return false;
        }

        CameraSdkStatus procStatus = CAMERA_STATUS_SUCCESS;
        bool copied = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (!running_ || !outBuffer_)
            {
                CameraReleaseImageBuffer(hCamera, pBuffer);
                return false;
            }
            procStatus = CameraImageProcess(hCamera_, pBuffer, outBuffer_, &frameHead);
            if (procStatus == CAMERA_STATUS_SUCCESS)
            {
                // Copy out of outBuffer_ while still holding stateMutex_ —
                // stop() frees the buffer under the same lock, so an unlocked
                // read here would race a concurrent stop (use-after-free).
                const int width = frameHead.iWidth;
                const int height = frameHead.iHeight;
                const std::size_t byteSize =
                    static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

                out.width = static_cast<std::uint64_t>(width);
                out.height = static_cast<std::uint64_t>(height);
                out.pixelFormat = kMono8PfncCode;
                out.linePitch = static_cast<std::size_t>(width);
                out.timestamp = static_cast<std::uint64_t>(frameHead.uiTimeStamp) * 100'000ULL;
                out.data.assign(outBuffer_, outBuffer_ + byteSize);
                ++frameCount_;
                copied = true;
            }
        }

        CameraReleaseImageBuffer(hCamera, pBuffer);

        if (!copied)
        {
            SPDLOG_WARN("MindVisionCamera: CameraImageProcess returned {}", procStatus);
            continue;
        }
        return true;
    }
}

bool MindVisionCamera::pollStats(CameraStats &out) const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_)
    {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - startTime_).count();
    if (elapsed > 0.0)
    {
        lastStats_.frameRate = static_cast<std::uint64_t>(static_cast<double>(frameCount_) / elapsed);
        const std::uint64_t bytesPerFrame = static_cast<std::uint64_t>(bufferWidth_) * static_cast<std::uint64_t>(bufferHeight_);
        lastStats_.dataRateMBps = (lastStats_.frameRate * bytesPerFrame) / (1024ULL * 1024ULL);
    }
    out = lastStats_;
    return true;
}

FrameDeliveryCapabilities MindVisionCamera::deliveryCapabilities() const
{
    FrameDeliveryCapabilities caps;
    caps.supportsEveryFrame = true;
    caps.supportsLatestFrame = true;
    caps.modeChangeRequiresRestart = true;
    // frameHead.uiTimeStamp is a device tick counter, not the host monotonic
    // clock, so frame age cannot be estimated against Tools::getTimestamp().
    caps.timestampsHostComparable = false;
    return caps;
}

FrameDeliveryMode MindVisionCamera::activeDeliveryMode() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return deliveryModeConfirmed_ ? confirmedDeliveryMode_ : config_.deliveryMode;
}

bool MindVisionCamera::pollAcquisitionQueueStats(AcquisitionQueueStats &out) const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_ || hCamera_ < 0)
    {
        return false;
    }

    out = AcquisitionQueueStats{};
    out.deliveredFrames = frameCount_;
    out.intentionallyDiscardedFrames = intentionalDiscards_.load(std::memory_order_relaxed);

    // The MindVision SDK exposes no completed-queue depth, input-buffer count,
    // or underrun counter, so those fields stay 0 with their valid flags false
    // ("unknown", not "zero") as required by issue #333.

    // Transport loss is observable through the SDK's cumulative frame
    // statistics (tSdkFrameStatistic: iTotal/iCapture/iLost, present in all
    // known SDK variants).
    tSdkFrameStatistic frameStat{};
    if (CameraGetFrameStatistic(hCamera_, &frameStat) == CAMERA_STATUS_SUCCESS)
    {
        out.transportLostFrames = frameStat.iLost > 0 ? static_cast<uint64_t>(frameStat.iLost) : 0;
        out.transportLossValid = true;
    }

    return true;
}

bool MindVisionCamera::checkDeviceHealth() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_ || hCamera_ < 0)
    {
        return false;
    }

    if (configuredTriggerMode_ != 0)
    {
        // Triggered acquisition: the frame probe below would consume a real
        // triggered frame (data loss), and "no frame within 100ms" is the
        // normal idle state, so the probe carries no health signal anyway.
        // A valid running handle is the best available check.
        // (CameraConnectTest would be the proper SDK probe, but the header is
        // resolved at build time from the installed SDK and the symbol's
        // presence in every deployed function table is unverified — revisit
        // once the Windows SDK version is pinned.)
        return true;
    }

    tSdkFrameHead frameHead{};
    BYTE *pBuffer = nullptr;
    const CameraSdkStatus status = CameraGetImageBuffer(hCamera_, &frameHead, &pBuffer, 100);
    if (status == CAMERA_STATUS_SUCCESS)
    {
        CameraReleaseImageBuffer(hCamera_, pBuffer);
        return true;
    }

    return status == CAMERA_STATUS_TIME_OUT;
}

void MindVisionCamera::configureTriggerOutput(const std::string &lineSelector)
{
    (void)lineSelector;
    constexpr int kSortOutputIndex = 1;

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (hCamera_ < 0)
    {
        triggerOutputIndex_ = kSortOutputIndex;
        SPDLOG_WARN("MindVisionCamera: configureTriggerOutput called before camera open");
        return;
    }

    CameraSdkStatus status = CameraSetOutPutIOMode(hCamera_, kSortOutputIndex, IOMODE_GP_OUTPUT);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetOutPutIOMode(OUT2, GP_OUTPUT) returned {}", status);
    }

    status = CameraSetIOStateEx(hCamera_, kSortOutputIndex, 0);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetIOStateEx(OUT2, LOW) returned {}", status);
    }

    triggerOutputIndex_ = kSortOutputIndex;
    SPDLOG_INFO("MindVisionCamera: trigger output configured on OUT2 (index {})", kSortOutputIndex);
}

bool MindVisionCamera::setTriggerOutput(bool high)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (triggerOutputIndex_ < 0 || !running_ || hCamera_ < 0)
    {
        return false;
    }

    const CameraSdkStatus status = CameraSetIOStateEx(hCamera_, triggerOutputIndex_, high ? 1 : 0);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetIOStateEx({}, {}) returned {}",
                    triggerOutputIndex_, high ? "HIGH" : "LOW", status);
        return false;
    }
    return true;
}

bool MindVisionCamera::softTrigger()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_ || hCamera_ < 0)
    {
        return false;
    }

    const CameraSdkStatus status = CameraSoftTrigger(hCamera_);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSoftTrigger returned {}", status);
        return false;
    }
    return true;
}

} // namespace camera::common

#else

#include <spdlog/spdlog.h>

namespace camera::common
{

MindVisionCamera::MindVisionCamera(int cameraIndex, std::string configPath)
    : cameraIndex_(cameraIndex), configPath_(std::move(configPath))
{
}

MindVisionCamera::~MindVisionCamera() = default;

void MindVisionCamera::applyConfig(const CameraConfig &config)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    config_ = config;
}

bool MindVisionCamera::start()
{
    SPDLOG_WARN("MindVisionCamera::start unavailable (MindVision SDK disabled at build time)");
    return false;
}

void MindVisionCamera::stop()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    running_ = false;
}

bool MindVisionCamera::grabFrame(Frame &out)
{
    (void)out;
    return false;
}

bool MindVisionCamera::pollStats(CameraStats &out) const
{
    (void)out;
    return false;
}

FrameDeliveryCapabilities MindVisionCamera::deliveryCapabilities() const
{
    return {};
}

FrameDeliveryMode MindVisionCamera::activeDeliveryMode() const
{
    return FrameDeliveryMode::EveryFrame;
}

bool MindVisionCamera::pollAcquisitionQueueStats(AcquisitionQueueStats &out) const
{
    (void)out;
    return false;
}

bool MindVisionCamera::checkDeviceHealth() const
{
    return false;
}

void MindVisionCamera::configureTriggerOutput(const std::string &lineSelector)
{
    (void)lineSelector;
}

bool MindVisionCamera::setTriggerOutput(bool high)
{
    (void)high;
    return false;
}

bool MindVisionCamera::softTrigger()
{
    return false;
}

} // namespace camera::common

#endif
