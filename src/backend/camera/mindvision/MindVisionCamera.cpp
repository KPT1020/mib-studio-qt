#include "backend/camera/mindvision/MindVisionCamera.h"

#ifndef MIB_HAS_MINDVISION
#define MIB_HAS_MINDVISION 0
#endif

#if MIB_HAS_MINDVISION

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#endif

#define API_LOAD_MAIN
#include "MindVision/CameraApiLoad.h"

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

    QJsonParseError parseErr{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    file.close();
    if (doc.isNull())
    {
        SPDLOG_WARN("MindVisionCamera: JSON parse error in {}: {}", configPath_, parseErr.errorString().toStdString());
        return false;
    }

    const QJsonObject obj = doc.object();
    const int width = obj.value("width").toInt(512);
    const int height = obj.value("height").toInt(96);
    const int offsetX = obj.value("offset_x").toInt(0);
    const int offsetY = obj.value("offset_y").toInt(0);
    const double exposureUs = obj.value("exposure_time_us").toDouble(3000.0);
    const int triggerMode = obj.value("trigger_mode").toInt(0);
    const int analogGain = obj.value("analog_gain").toInt(1);
    const bool aeEnabled = obj.value("auto_exposure_enabled").toBool(false);
    const int aeTarget = obj.value("ae_target_brightness").toInt(100);
    const int gamma = obj.value("gamma").toInt(100);
    const int contrast = obj.value("contrast").toInt(100);
    const int sharpness = obj.value("sharpness").toInt(0);
    const int frameSpeed = obj.value("frame_speed").toInt(2);
    const bool flipHorizontal = obj.value("flip_horizontal").toBool(false);
    const bool flipVertical = obj.value("flip_vertical").toBool(false);
    const int strobeMode = obj.value("strobe_mode").toInt(0);
    const int strobePulseUs = obj.value("strobe_pulse_width_us").toInt(500);
    const int strobeDelayUs = obj.value("strobe_delay_us").toInt(0);
    const int strobePolarity = obj.value("strobe_polarity").toInt(1);

    SPDLOG_INFO("MindVisionCamera: applying config w={} h={} ox={} oy={} exp={} trig={} gain={}",
                width, height, offsetX, offsetY, exposureUs, triggerMode, analogGain);

    tSdkImageResolution res{};
    res.iIndex = 0xFF;
    res.iHOffsetFOV = offsetX;
    res.iVOffsetFOV = offsetY;
    res.iWidthFOV = width;
    res.iHeightFOV = height;
    res.iWidth = width;
    res.iHeight = height;

    CameraSdkStatus status = CameraSetImageResolution(hCamera, &res);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetImageResolution returned {}", status);
    }

    status = CameraSetExposureTime(hCamera, exposureUs);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetExposureTime returned {}", status);
    }

    status = CameraSetTriggerMode(hCamera, triggerMode);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetTriggerMode returned {}", status);
    }

    status = CameraSetAnalogGain(hCamera, analogGain);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetAnalogGain returned {}", status);
    }

    status = CameraSetAeState(hCamera, aeEnabled ? TRUE : FALSE);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetAeState returned {}", status);
    }

    status = CameraSetAeTarget(hCamera, aeTarget);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetAeTarget returned {}", status);
    }

    status = CameraSetGamma(hCamera, gamma);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetGamma returned {}", status);
    }

    status = CameraSetContrast(hCamera, contrast);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetContrast returned {}", status);
    }

    status = CameraSetSharpness(hCamera, sharpness);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetSharpness returned {}", status);
    }

    status = CameraSetFrameSpeed(hCamera, frameSpeed);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetFrameSpeed returned {}", status);
    }

    status = CameraSetMirror(hCamera, 0, flipHorizontal ? TRUE : FALSE);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetMirror(H) returned {}", status);
    }

    status = CameraSetMirror(hCamera, 1, flipVertical ? TRUE : FALSE);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetMirror(V) returned {}", status);
    }

    status = CameraSetStrobeMode(hCamera, strobeMode);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetStrobeMode returned {}", status);
    }

    status = CameraSetStrobePulseWidth(hCamera, static_cast<UINT>(strobePulseUs));
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetStrobePulseWidth returned {}", status);
    }

    status = CameraSetStrobeDelayTime(hCamera, static_cast<UINT>(strobeDelayUs));
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetStrobeDelayTime returned {}", status);
    }

    status = CameraSetStrobePolarity(hCamera, strobePolarity);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        SPDLOG_WARN("MindVisionCamera: CameraSetStrobePolarity returned {}", status);
    }

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
    running_ = true;
    SPDLOG_INFO("MindVisionCamera: started (index={}, {}x{}, mono={})",
                cameraIndex_, bufferWidth_, bufferHeight_, static_cast<int>(cap.sIspCapacity.bMonoSensor));
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

bool MindVisionCamera::checkDeviceHealth() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_ || hCamera_ < 0)
    {
        return false;
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
