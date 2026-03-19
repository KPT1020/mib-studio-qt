// Define API_LOAD_MAIN before including CameraApiLoad.h so this translation
// unit provides all global function-pointer definitions and LoadSdkApi().
// CameraApiLoad.h must NOT be included with API_LOAD_MAIN in any other TU.
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>   // sprintf_s used in CameraApiLoad.h LoadSdkApi()
#endif
#define API_LOAD_MAIN
#include "MindVision/CameraApiLoad.h"

#include "camera/common/MindVisionCamera.h"
#include "camera/common/Frame.h"

#include <spdlog/spdlog.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <cstring>
#include <string>

namespace camera::common {

// PFNC Mono8 pixel format code (matches Euresys downstream expectations)
static constexpr uint64_t kMono8PfncCode = 0x01080001u;

MindVisionCamera::MindVisionCamera(int cameraIndex, std::string configPath)
    : cameraIndex_(cameraIndex), configPath_(std::move(configPath))
{}

MindVisionCamera::~MindVisionCamera() {
    if (running_) {
        stop();
    }
}

void MindVisionCamera::applyConfig(const CameraConfig& config) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    config_ = config;
}

bool MindVisionCamera::applyJsonConfig(int hCamera) {
    if (configPath_.empty()) {
        return true;
    }

    QFile f(QString::fromStdString(configPath_));
    if (!f.open(QIODevice::ReadOnly)) {
        SPDLOG_WARN("MindVisionCamera: cannot open config file: {}", configPath_);
        return false;
    }
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseErr);
    f.close();
    if (doc.isNull()) {
        SPDLOG_WARN("MindVisionCamera: JSON parse error in {}: {}",
                    configPath_, parseErr.errorString().toStdString());
        return false;
    }
    QJsonObject obj = doc.object();

    int    width       = obj.value("width").toInt(512);
    int    height      = obj.value("height").toInt(96);
    int    offset_x    = obj.value("offset_x").toInt(0);
    int    offset_y    = obj.value("offset_y").toInt(0);
    double expUs       = obj.value("exposure_time_us").toDouble(3000.0);
    int    triggerMode = obj.value("trigger_mode").toInt(0);
    int    analogGain  = obj.value("analog_gain").toInt(1);

    SPDLOG_INFO("MindVisionCamera: applying config w={} h={} ox={} oy={} exp={} trig={} gain={}",
                width, height, offset_x, offset_y, expUs, triggerMode, analogGain);

    // Resolution — custom ROI (iIndex=0xFF)
    tSdkImageResolution res{};
    res.iIndex      = 0xFF;
    res.iHOffsetFOV = offset_x;
    res.iVOffsetFOV = offset_y;
    res.iWidthFOV   = width;
    res.iHeightFOV  = height;
    res.iWidth      = width;
    res.iHeight     = height;

    CameraSdkStatus s = CameraSetImageResolution(hCamera, &res);
    if (s != CAMERA_STATUS_SUCCESS) {
        SPDLOG_WARN("MindVisionCamera: CameraSetImageResolution returned {}", s);
    }

    s = CameraSetExposureTime(hCamera, expUs);
    if (s != CAMERA_STATUS_SUCCESS) {
        SPDLOG_WARN("MindVisionCamera: CameraSetExposureTime returned {}", s);
    }

    s = CameraSetTriggerMode(hCamera, triggerMode);
    if (s != CAMERA_STATUS_SUCCESS) {
        SPDLOG_WARN("MindVisionCamera: CameraSetTriggerMode returned {}", s);
    }

    s = CameraSetAnalogGain(hCamera, analogGain);
    if (s != CAMERA_STATUS_SUCCESS) {
        SPDLOG_WARN("MindVisionCamera: CameraSetAnalogGain returned {}", s);
    }

    return true;
}

bool MindVisionCamera::start() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (running_) {
        return true;
    }

    // Load the MindVision SDK DLL (idempotent – safe to call multiple times)
    if (LoadSdkApi() != CAMERA_STATUS_SUCCESS) {
        SPDLOG_ERROR("MindVisionCamera: failed to load MVCAMSDK DLL");
        return false;
    }

    CameraSdkStatus status = CameraSdkInit(0); // 0 = English
    if (status != CAMERA_STATUS_SUCCESS) {
        // Non-fatal on some SDK versions; log and continue
        SPDLOG_WARN("MindVisionCamera: CameraSdkInit returned {}", status);
    }

    // Enumerate devices to get tSdkCameraDevInfo for CameraInit
    tSdkCameraDevInfo devList[32];
    INT count = 32;
    status = CameraEnumerateDevice(devList, &count);
    if (status != CAMERA_STATUS_SUCCESS || count == 0) {
        SPDLOG_ERROR("MindVisionCamera: CameraEnumerateDevice failed (status={}, count={})",
                     status, count);
        return false;
    }

    if (cameraIndex_ < 0 || cameraIndex_ >= count) {
        SPDLOG_ERROR("MindVisionCamera: cameraIndex={} out of range (found {})",
                     cameraIndex_, count);
        return false;
    }

    CameraHandle hCamera = -1;
    status = CameraInit(&devList[cameraIndex_], -1, -1, &hCamera);
    if (status != CAMERA_STATUS_SUCCESS) {
        SPDLOG_ERROR("MindVisionCamera: CameraInit failed (status={})", status);
        return false;
    }
    hCamera_ = hCamera;

    // Query capability to detect mono vs. colour sensor
    tSdkCameraCapbility cap;
    status = CameraGetCapability(hCamera_, &cap);
    if (status != CAMERA_STATUS_SUCCESS) {
        SPDLOG_ERROR("MindVisionCamera: CameraGetCapability failed (status={})", status);
        CameraUnInit(hCamera_);
        hCamera_ = -1;
        return false;
    }

    // Apply JSON config (resolution, exposure, trigger, gain) in the same
    // camera session so the capture buffers are sized for the configured ROI.
    applyJsonConfig(hCamera_);

    // Read back the actual resolution after applying config — this determines
    // the buffer allocation size and is the ground truth for what the SDK uses.
    tSdkImageResolution res;
    status = CameraGetImageResolution(hCamera_, &res);
    if (status != CAMERA_STATUS_SUCCESS) {
        SPDLOG_ERROR("MindVisionCamera: CameraGetImageResolution failed (status={})", status);
        CameraUnInit(hCamera_);
        hCamera_ = -1;
        return false;
    }
    bufferWidth_  = res.iWidth;
    bufferHeight_ = res.iHeight;

    // Set ISP output format to MONO8 for mono sensors
    if (cap.sIspCapacity.bMonoSensor) {
        status = CameraSetIspOutFormat(hCamera_, CAMERA_MEDIA_TYPE_MONO8);
        if (status != CAMERA_STATUS_SUCCESS) {
            SPDLOG_WARN("MindVisionCamera: CameraSetIspOutFormat(MONO8) returned {}", status);
        }
    }

    // Note: trigger mode is not forced here — it is set by applyMindVisionConfig
    // before capture starts (via onTabChanged or the Apply button).

    // Allocate 16-byte aligned output buffer (MONO8 = 1 byte/pixel)
    const int bufSize = bufferWidth_ * bufferHeight_ * 1;
    outBuffer_ = CameraAlignMalloc(bufSize, 16);
    if (!outBuffer_) {
        SPDLOG_ERROR("MindVisionCamera: CameraAlignMalloc({}) failed", bufSize);
        CameraUnInit(hCamera_);
        hCamera_ = -1;
        return false;
    }

    status = CameraPlay(hCamera_);
    if (status != CAMERA_STATUS_SUCCESS) {
        SPDLOG_ERROR("MindVisionCamera: CameraPlay failed (status={})", status);
        CameraAlignFree(outBuffer_);
        outBuffer_ = nullptr;
        CameraUnInit(hCamera_);
        hCamera_ = -1;
        return false;
    }

    frameCount_ = 0;
    startTime_  = std::chrono::steady_clock::now();
    running_    = true;

    SPDLOG_INFO("MindVisionCamera: started (index={}, {}x{}, mono={})",
                cameraIndex_, bufferWidth_, bufferHeight_,
                static_cast<int>(cap.sIspCapacity.bMonoSensor));
    return true;
}

void MindVisionCamera::stop() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_) {
        return;
    }
    running_ = false;

    if (hCamera_ >= 0) {
        CameraStop(hCamera_);
        if (outBuffer_) {
            CameraAlignFree(outBuffer_);
            outBuffer_ = nullptr;
        }
        CameraUnInit(hCamera_);
        hCamera_ = -1;
    }
    SPDLOG_INFO("MindVisionCamera: stopped");
}

bool MindVisionCamera::grabFrame(Frame& out) {
    // Poll in a loop with short timeouts so stop() can interrupt promptly.
    while (true) {
        int hCamera;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (!running_) {
                return false;
            }
            hCamera = hCamera_;
        }

        tSdkFrameHead frameHead{};
        BYTE* pBuffer = nullptr;
        // 100 ms timeout keeps stop() latency short
        const CameraSdkStatus status = CameraGetImageBuffer(hCamera, &frameHead, &pBuffer, 100);

        if (status == CAMERA_STATUS_TIME_OUT) {
            // No frame yet – check if still running and retry
            continue;
        }

        if (status != CAMERA_STATUS_SUCCESS) {
            // Camera error or disconnected
            SPDLOG_WARN("MindVisionCamera: CameraGetImageBuffer returned {}", status);
            return false;
        }

        // Process raw buffer through ISP into outBuffer_
        CameraSdkStatus procStatus;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (!running_ || !outBuffer_) {
                CameraReleaseImageBuffer(hCamera, pBuffer);
                return false;
            }
            procStatus = CameraImageProcess(hCamera_, pBuffer, outBuffer_, &frameHead);
        }

        CameraReleaseImageBuffer(hCamera, pBuffer);

        if (procStatus != CAMERA_STATUS_SUCCESS) {
            SPDLOG_WARN("MindVisionCamera: CameraImageProcess returned {}", procStatus);
            continue;
        }

        // Populate output frame
        const int w = frameHead.iWidth;
        const int h = frameHead.iHeight;

        out.width       = static_cast<uint64_t>(w);
        out.height      = static_cast<uint64_t>(h);
        out.pixelFormat = kMono8PfncCode;
        out.linePitch   = static_cast<size_t>(w);
        // MindVision timestamp unit is 0.1 ms = 100 µs = 100,000 ns
        out.timestamp   = static_cast<uint64_t>(frameHead.uiTimeStamp) * 100'000ULL;
        out.data.assign(outBuffer_, outBuffer_ + static_cast<size_t>(w) * h);

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            ++frameCount_;
        }
        return true;
    }
}

bool MindVisionCamera::pollStats(CameraStats& out) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_) {
        return false;
    }

    const auto now     = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - startTime_).count();
    if (elapsed > 0.0) {
        lastStats_.frameRate    = static_cast<uint64_t>(static_cast<double>(frameCount_) / elapsed);
        const uint64_t bytesPerFrame = static_cast<uint64_t>(bufferWidth_) * bufferHeight_;
        lastStats_.dataRateMBps = (lastStats_.frameRate * bytesPerFrame) / (1024ULL * 1024ULL);
    }
    out = lastStats_;
    return true;
}

bool MindVisionCamera::checkDeviceHealth() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_ || hCamera_ < 0) {
        return false;
    }

    // Attempt a short-timeout frame grab as a liveness check
    tSdkFrameHead frameHead{};
    BYTE* pBuffer = nullptr;
    const CameraSdkStatus status = CameraGetImageBuffer(hCamera_, &frameHead, &pBuffer, 100);

    if (status == CAMERA_STATUS_SUCCESS) {
        CameraReleaseImageBuffer(hCamera_, pBuffer);
        return true;
    }
    // Timeout is acceptable: camera is alive but no frame arrived in 100 ms
    return status == CAMERA_STATUS_TIME_OUT;
}

} // namespace camera::common
