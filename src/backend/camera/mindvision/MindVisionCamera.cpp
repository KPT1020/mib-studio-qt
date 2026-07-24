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

#include "backend/camera/mindvision/MindVisionConfigApply.h"

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
    // bufferPartCount/numBuffers are EGrabber DMA-tuning knobs; the MVSDK owns
    // its internal buffer queue and exposes no equivalent, so they are
    // intentionally not applied here.
    SPDLOG_DEBUG("MindVisionCamera: CameraConfig buffer settings (parts={}, buffers={}) not applicable to MVSDK",
                 config.bufferPartCount, config.numBuffers);
}

bool MindVisionCamera::applyJsonConfig(int hCamera)
{
    if (configPath_.empty())
    {
        return true;
    }

    const auto result = backend::camera::mindvision::applyJsonFileToCamera(hCamera, configPath_);
    for (const auto &warning : result.warnings)
    {
        SPDLOG_WARN("MindVisionCamera: {} (in {})", warning, configPath_);
    }
    if (!result.ok)
    {
        SPDLOG_WARN("MindVisionCamera: {}", result.error);
        return false;
    }

    requestedTriggerOutputIndex_ = result.config.triggerOutputIndex;
    return true;
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

    if (cap.sIspCapacity.bMonoSensor)
    {
        status = CameraSetIspOutFormat(hCamera_, CAMERA_MEDIA_TYPE_MONO8);
        if (status != CAMERA_STATUS_SUCCESS)
        {
            SPDLOG_WARN("MindVisionCamera: CameraSetIspOutFormat(MONO8) returned {}", status);
        }
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
    startTime_ = std::chrono::steady_clock::now();
    frameRate_.reset();
    {
        std::lock_guard<std::mutex> triggerLock(triggerMutex_);
        triggerCameraHandle_ = hCamera_;
    }
    running_.store(true, std::memory_order_release);
    SPDLOG_INFO("MindVisionCamera: started (index={}, {}x{}, mono={})",
                cameraIndex_, bufferWidth_, bufferHeight_, static_cast<int>(cap.sIspCapacity.bMonoSensor));
    return true;
}

void MindVisionCamera::stop()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_.load(std::memory_order_acquire))
    {
        return;
    }
    running_.store(false, std::memory_order_release);

    // Invalidate the handle for the trigger path before tearing it down so an
    // in-flight pulse can never race CameraUnInit.
    {
        std::lock_guard<std::mutex> triggerLock(triggerMutex_);
        triggerCameraHandle_ = -1;
    }

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
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (!running_)
            {
                return false;
            }
            hCamera = hCamera_;
        }

        tSdkFrameHead frameHead{};
        BYTE *pBuffer = nullptr;
        const CameraSdkStatus status = CameraGetImageBuffer(hCamera, &frameHead, &pBuffer, 100);

        if (status == CAMERA_STATUS_TIME_OUT)
        {
            continue;
        }
        if (status != CAMERA_STATUS_SUCCESS)
        {
            SPDLOG_WARN("MindVisionCamera: CameraGetImageBuffer returned {}", status);
            return false;
        }

        CameraSdkStatus procStatus;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (!running_ || !outBuffer_)
            {
                CameraReleaseImageBuffer(hCamera, pBuffer);
                return false;
            }
            procStatus = CameraImageProcess(hCamera_, pBuffer, outBuffer_, &frameHead);
        }

        CameraReleaseImageBuffer(hCamera, pBuffer);

        if (procStatus != CAMERA_STATUS_SUCCESS)
        {
            SPDLOG_WARN("MindVisionCamera: CameraImageProcess returned {}", procStatus);
            continue;
        }

        const int width = frameHead.iWidth;
        const int height = frameHead.iHeight;
        const std::size_t byteSize = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

        out.width = static_cast<std::uint64_t>(width);
        out.height = static_cast<std::uint64_t>(height);
        out.pixelFormat = kMono8PfncCode;
        out.linePitch = static_cast<std::size_t>(width);
        out.timestamp = static_cast<std::uint64_t>(frameHead.uiTimeStamp) * 100'000ULL;
        out.data.assign(outBuffer_, outBuffer_ + byteSize);

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            ++frameCount_;
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

    // Host-computed rate over the window since the previous poll (EGrabber
    // reports device-side instantaneous rates; a cumulative since-start
    // average here would go stale and dilute to nothing in trigger mode).
    const double rate = frameRate_.sample(frameCount_, std::chrono::steady_clock::now());
    lastStats_.frameRate = static_cast<std::uint64_t>(rate);
    const std::uint64_t bytesPerFrame = static_cast<std::uint64_t>(bufferWidth_) * static_cast<std::uint64_t>(bufferHeight_);
    lastStats_.dataRateMBps = static_cast<std::uint64_t>(rate * static_cast<double>(bytesPerFrame)) / (1024ULL * 1024ULL);
    out = lastStats_;
    return true;
}

bool MindVisionCamera::checkDeviceHealth() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_ || hCamera_ < 0)
    {
        return false;
    }

    // Must never grab from the image queue here: the health check runs on the
    // capture thread every few seconds, and a probe grab consumes (and
    // discards) a real frame — in trigger mode possibly a triggered target
    // frame — while stalling grabFrame for the probe timeout.
    tSdkFrameStatistic stat{};
    const CameraSdkStatus status = CameraGetFrameStatistic(hCamera_, &stat);
    return status == CAMERA_STATUS_SUCCESS;
}

void MindVisionCamera::configureTriggerOutput(const std::string &lineSelector)
{
    // lineSelector carries a Euresys GenICam line name that has no MVSDK
    // equivalent; the output line comes from the JSON config's
    // trigger_output_index (default 1 = OUT2) instead.
    (void)lineSelector;

    // triggerMutex_ (not stateMutex_) so the trigger path never queues behind
    // grabFrame's image processing or stop()'s teardown.
    std::lock_guard<std::mutex> triggerLock(triggerMutex_);
    const int outputIndex = requestedTriggerOutputIndex_.load(std::memory_order_acquire);
    if (triggerCameraHandle_ < 0)
    {
        triggerOutputIndex_ = outputIndex;
        SPDLOG_WARN("MindVisionCamera: configureTriggerOutput called before camera open");
        return;
    }

    CameraSdkStatus status = CameraSetOutPutIOMode(triggerCameraHandle_, outputIndex, IOMODE_GP_OUTPUT);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetOutPutIOMode({}, GP_OUTPUT) returned {}", outputIndex, status);
    }

    status = CameraSetIOStateEx(triggerCameraHandle_, outputIndex, 0);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetIOStateEx({}, LOW) returned {}", outputIndex, status);
    }

    triggerOutputIndex_ = outputIndex;
    SPDLOG_INFO("MindVisionCamera: trigger output configured on GPIO index {}", outputIndex);
}

bool MindVisionCamera::setTriggerOutput(bool high)
{
    // triggerMutex_ pins the handle alive for the duration of the SDK call;
    // stop() invalidates triggerCameraHandle_ under the same mutex before
    // CameraUnInit, so this can never touch a dead handle.
    std::lock_guard<std::mutex> triggerLock(triggerMutex_);
    if (triggerOutputIndex_ < 0 || triggerCameraHandle_ < 0 ||
        !running_.load(std::memory_order_acquire))
    {
        return false;
    }

    const CameraSdkStatus status = CameraSetIOStateEx(triggerCameraHandle_, triggerOutputIndex_, high ? 1 : 0);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetIOStateEx({}, {}) returned {}",
                    triggerOutputIndex_, high ? "HIGH" : "LOW", status);
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

} // namespace camera::common

#endif
