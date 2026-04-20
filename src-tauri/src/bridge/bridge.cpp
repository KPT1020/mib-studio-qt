#include "bridge/bridge.h"

#include "backend/AppBackend.h"
#include "backend/playback/FrameStore.h"
#include "backend/services/CameraControlService.h"
#include "backend/services/CameraControlTypes.h"
#include "backend/services/CaptureService.h"
#include "backend/services/PlaybackService.h"
#include "backend/services/ProcessingService.h"
#include "camera/mock/MockCamera.h"

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "rust/cxx.h"

// cxx shared structs (must match ffi.rs); include after bridge.h via cxxbridge header.
#include "mib-studio/src/bridge/ffi.rs.h"

namespace {

constexpr std::uint64_t kPfncMono8 = 0x01080001ULL;

rust::Vec<std::uint8_t> encode_frame_png(const std::uint8_t* data,
                                         std::size_t size,
                                         std::uint64_t width,
                                         std::uint64_t height,
                                         std::size_t line_pitch,
                                         std::uint64_t pixel_format) {
    rust::Vec<std::uint8_t> out;
    if (!data || size == 0 || width == 0 || height == 0) {
        return out;
    }

    int cv_type = CV_8UC1;
    if (pixel_format == kPfncMono8 || pixel_format == 0) {
        cv_type = CV_8UC1;
    } else {
        if (size == width * height * 3) {
            cv_type = CV_8UC3;
        } else {
            cv_type = CV_8UC1;
        }
    }

    cv::Mat mat;
    if (cv_type == CV_8UC1) {
        const int w = static_cast<int>(width);
        const int h = static_cast<int>(height);
        const int pitch = line_pitch > 0 ? static_cast<int>(line_pitch) : w;
        mat = cv::Mat(h, w, CV_8UC1, const_cast<std::uint8_t*>(data), pitch);
    } else {
        mat = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC3,
                      const_cast<std::uint8_t*>(data));
    }

    std::vector<std::uint8_t> buf;
    if (!cv::imencode(".png", mat, buf)) {
        return out;
    }
    out.reserve(buf.size());
    for (std::uint8_t b : buf) {
        out.push_back(b);
    }
    return out;
}

BridgeCamera to_bridge_camera(const backend::services::DiscoveredCamera& c) {
    BridgeCamera out{};
    out.interface_index = c.interfaceIndex;
    out.device_index = c.deviceIndex;
    out.interface_id = rust::String(c.interfaceID);
    out.device_id = rust::String(c.deviceID);
    out.model_name = rust::String(c.modelName);
    out.firmware_version = rust::String(c.firmwareVersion);
    out.label = rust::String(c.label);
    return out;
}

BridgeFramegrabber to_bridge_fg(const backend::services::DiscoveredFramegrabber& f) {
    BridgeFramegrabber out{};
    out.interface_index = f.interfaceIndex;
    out.device_index = f.deviceIndex;
    out.stream_index = f.streamIndex;
    out.interface_id = rust::String(f.interfaceID);
    out.device_id = rust::String(f.deviceID);
    out.stream_id = rust::String(f.streamID);
    out.model_name = rust::String(f.modelName);
    out.label = rust::String(f.label);
    return out;
}

} // namespace

AppBackendShim::AppBackendShim(std::unique_ptr<backend::AppBackend> backend)
    : backend_(std::move(backend)) {}

AppBackendShim::~AppBackendShim() {
    stats_stop_.store(true, std::memory_order_relaxed);
    if (stats_thread_.joinable()) {
        stats_thread_.join();
    }
    if (backend_) {
        backend_->capture().setFrameCallback(nullptr);
        backend_->processing().stopRealtime();
        if (backend_->capture().isRunning()) {
            backend_->capture().stop();
        }
    }
}

rust::String AppBackendShim::backend_version() const {
    return rust::String("mib_backend dev");
}

void AppBackendShim::on_frame(const std::uint8_t* data,
                              std::size_t size,
                              std::uint64_t width,
                              std::uint64_t height,
                              std::size_t line_pitch,
                              std::uint64_t pixel_format,
                              std::uint64_t timestamp_ns) {
    if (!backend_ || !data || size == 0) {
        return;
    }
    auto store = backend_->getFrameStore();
    const std::uint64_t idx = store ? store->totalWritten() : 0;

    rust::Vec<std::uint8_t> png =
        encode_frame_png(data, size, width, height, line_pitch, pixel_format);
    mib_emit_frame(idx, width, height, timestamp_ns, std::move(png));
}

void AppBackendShim::stats_loop() {
    while (!stats_stop_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!backend_ || stats_stop_.load(std::memory_order_relaxed)) {
            break;
        }
        const auto& cap = backend_->capture().stats();
        const double cap_fps = static_cast<double>(cap.lastFrameRate.load(std::memory_order_relaxed));
        const double cap_mbps = static_cast<double>(cap.lastDataRateMBps.load(std::memory_order_relaxed));
        auto& proc = backend_->processing();
        mib_emit_stats(cap_fps,
                       cap_mbps,
                       proc.getAlgoFps1s(),
                       proc.getValidFps1s(),
                       proc.getInvalidFps1s(),
                       proc.getAlgoAvgUs1s(),
                       proc.getTotalValidFlushed());
    }
}

void AppBackendShim::install_emitters_impl() {
    if (!backend_) {
        return;
    }
    bool expected = false;
    if (!emitters_installed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    backend_->processing().startRealtime(backend_->getFrameStore());

    backend_->capture().setFrameCallback(
        [this](const std::uint8_t* data,
               std::size_t size,
               std::uint64_t width,
               std::uint64_t height,
               std::size_t line_pitch,
               std::uint64_t pixel_format,
               std::uint64_t timestamp_ns) {
            on_frame(data, size, width, height, line_pitch, pixel_format, timestamp_ns);
        });

    stats_stop_.store(false, std::memory_order_relaxed);
    stats_thread_ = std::thread(&AppBackendShim::stats_loop, this);
}

rust::Vec<BridgeCamera> bridge_discover_cameras(const AppBackendShim& shim) {
    rust::Vec<BridgeCamera> out;
    if (!shim.app_backend_ptr()) {
        return out;
    }
    const auto cams = shim.app_backend().cameraControl().discoverCameras();
    out.reserve(cams.size());
    for (const auto& c : cams) {
        out.push_back(to_bridge_camera(c));
    }
    return out;
}

rust::Vec<BridgeFramegrabber> bridge_discover_framegrabbers(const AppBackendShim& shim) {
    rust::Vec<BridgeFramegrabber> out;
    if (!shim.app_backend_ptr()) {
        return out;
    }
    const auto fgs = shim.app_backend().cameraControl().discoverFramegrabbers();
    out.reserve(fgs.size());
    for (const auto& f : fgs) {
        out.push_back(to_bridge_fg(f));
    }
    return out;
}

void bridge_set_hardware_camera(const AppBackendShim& shim,
                                std::int32_t interface_index,
                                std::int32_t device_index,
                                rust::Str label) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    std::string lbl(label.data(), label.size());
    shim.app_backend().setHardwareCameraSelection(interface_index, device_index, lbl);
}

bool bridge_configure_mock(const AppBackendShim& shim,
                           rust::Str dir,
                           std::uint32_t interval_ms,
                           bool loop_files) {
    if (!shim.app_backend_ptr()) {
        return false;
    }
    camera::mock::MockCameraOptions options;
    options.folder = std::filesystem::path(std::string(dir.data(), dir.size()));
    options.frameInterval = std::chrono::milliseconds(interval_ms > 0 ? interval_ms : 33);
    options.loopFiles = loop_files;
    shim.app_backend().configureMockCamera(options);
    return true;
}

bool bridge_start_capture(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return false;
    }
    return shim.app_backend().capture().start();
}

void bridge_stop_capture(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().capture().stop();
}

bool bridge_is_capture_running(const AppBackendShim& shim) {
    return shim.app_backend_ptr() && shim.app_backend().capture().isRunning();
}

rust::Vec<std::uint8_t> bridge_fetch_latest_frame_png(const AppBackendShim& shim) {
    rust::Vec<std::uint8_t> out;
    if (!shim.app_backend_ptr()) {
        return out;
    }
    backend::playback::Frame f;
    if (!shim.app_backend().playback().fetchLatest(f) || f.data.empty()) {
        return out;
    }
    return encode_frame_png(f.data.data(),
                            f.data.size(),
                            f.width,
                            f.height,
                            f.linePitch,
                            f.pixelFormat);
}

BridgeFrameMeta bridge_fetch_latest_frame_meta(const AppBackendShim& shim) {
    BridgeFrameMeta m{};
    if (!shim.app_backend_ptr()) {
        return m;
    }
    backend::playback::Frame f;
    if (!shim.app_backend().playback().fetchLatest(f) || f.data.empty()) {
        return m;
    }
    const auto store = shim.app_backend().getFrameStore();
    const std::uint64_t tw = store ? store->totalWritten() : 0;
    m.index = tw > 0 ? tw - 1 : 0;
    m.width = f.width;
    m.height = f.height;
    m.timestamp_ns = f.timestamp;
    return m;
}

BridgePlaybackRange bridge_get_playback_range(const AppBackendShim& shim) {
    BridgePlaybackRange r{};
    if (!shim.app_backend_ptr()) {
        return r;
    }
    std::uint64_t earliest = 0;
    std::uint64_t latest = 0;
    std::size_t count = 0;
    if (!shim.app_backend().playback().queryRange(earliest, latest, count)) {
        return r;
    }
    r.earliest = earliest;
    r.latest = latest;
    r.count = static_cast<std::uint64_t>(count);
    return r;
}

void bridge_install_emitters(AppBackendShim& shim) {
    shim.install_emitters_impl();
}

std::unique_ptr<AppBackendShim> create_shim(rust::Str data_dir) {
    auto backend = std::make_unique<backend::AppBackend>();
    std::string dir(data_dir.data(), data_dir.size());
    if (!backend->initialize(dir)) {
        return nullptr;
    }
    return std::make_unique<AppBackendShim>(std::move(backend));
}
