#include "bridge/bridge.h"

#include "backend/AppBackend.h"
#include "backend/playback/FrameStore.h"
#include "backend/services/AutofocusService.h"
#include "backend/services/CameraControlService.h"
#include "backend/services/CameraControlTypes.h"
#include "backend/services/CaptureService.h"
#include "backend/services/Hdf5Service.h"
#include "backend/services/PlaybackService.h"
#include "backend/services/ProcessingService.h"
#include "backend/services/SyringePumpService.h"
#include "backend/services/TriggerService.h"
#include "camera/mock/MockCamera.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include "rust/cxx.h"

// cxx shared structs (must match ffi.rs); include after bridge.h via cxxbridge header.
#include "mib-studio/src/bridge/ffi.rs.h"

namespace {

constexpr std::uint64_t kPfncMono8 = 0x01080001ULL;
constexpr char kB64Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string to_std_string(rust::Str value) {
    return std::string(value.data(), value.size());
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool has_hdf5_extension(const std::string& path) {
    const std::string lower = to_lower(path);
    return (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".h5") ||
           (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".hdf5");
}

std::string ensure_hdf5_extension(std::string path) {
    if (!has_hdf5_extension(path)) {
        path += ".h5";
    }
    return path;
}

std::uint64_t now_unix_ns() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

std::string base64_encode(const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0) {
        return {};
    }

    std::string out;
    out.reserve(((size + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 3 <= size) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                    static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(kB64Table[(chunk >> 18) & 0x3F]);
        out.push_back(kB64Table[(chunk >> 12) & 0x3F]);
        out.push_back(kB64Table[(chunk >> 6) & 0x3F]);
        out.push_back(kB64Table[chunk & 0x3F]);
        i += 3;
    }

    const std::size_t rem = size - i;
    if (rem == 1) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(kB64Table[(chunk >> 18) & 0x3F]);
        out.push_back(kB64Table[(chunk >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kB64Table[(chunk >> 18) & 0x3F]);
        out.push_back(kB64Table[(chunk >> 12) & 0x3F]);
        out.push_back(kB64Table[(chunk >> 6) & 0x3F]);
        out.push_back('=');
    }

    return out;
}

rust::Vec<std::uint8_t> vec_to_rust(const std::vector<std::uint8_t>& in) {
    rust::Vec<std::uint8_t> out;
    out.reserve(in.size());
    for (std::uint8_t b : in) {
        out.push_back(b);
    }
    return out;
}

std::vector<std::uint8_t> encode_mat_png(const cv::Mat& image) {
    if (image.empty()) {
        return {};
    }
    cv::Mat prepared = image;
    if (prepared.depth() != CV_8U) {
        prepared.convertTo(prepared, CV_8U);
    }
    if (prepared.channels() != 1 && prepared.channels() != 3 && prepared.channels() != 4) {
        cv::Mat gray;
        cv::cvtColor(prepared, gray, cv::COLOR_BGR2GRAY);
        prepared = std::move(gray);
    }
    std::vector<std::uint8_t> png;
    if (!cv::imencode(".png", prepared, png)) {
        return {};
    }
    return png;
}

rust::Vec<std::uint8_t> encode_frame_png(const std::uint8_t* data,
                                         std::size_t size,
                                         std::uint64_t width,
                                         std::uint64_t height,
                                         std::size_t line_pitch,
                                         std::uint64_t pixel_format) {
    if (!data || size == 0 || width == 0 || height == 0) {
        return {};
    }

    int cv_type = CV_8UC1;
    if (!(pixel_format == kPfncMono8 || pixel_format == 0) && size == width * height * 3) {
        cv_type = CV_8UC3;
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

    return vec_to_rust(encode_mat_png(mat));
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

BridgeProcessingConfig to_bridge_processing_config(const backend::services::ProcessingConfig& cfg) {
    BridgeProcessingConfig out{};
    out.gaussian_blur_size = cfg.gaussian_blur_size;
    out.bg_subtract_threshold = cfg.bg_subtract_threshold;
    out.morph_kernel_size = cfg.morph_kernel_size;
    out.morph_iterations = cfg.morph_iterations;
    out.area_threshold_min = cfg.area_threshold_min;
    out.area_threshold_max = cfg.area_threshold_max;
    out.deformability_threshold_min = cfg.deformability_threshold_min;
    out.deformability_threshold_max = cfg.deformability_threshold_max;
    out.enable_border_check = cfg.enable_border_check;
    out.enable_area_range_check = cfg.enable_area_range_check;
    out.enable_deformability_range_check = cfg.enable_deformability_range_check;
    out.area_ratio_threshold_max = cfg.area_ratio_threshold_max;
    out.enable_area_ratio_check = cfg.enable_area_ratio_check;
    out.require_single_inner_contour = cfg.require_single_inner_contour;
    out.empty_frame_pixel_threshold = cfg.empty_frame_pixel_threshold;
    out.auto_background_enabled = cfg.auto_background_enabled;
    out.auto_background_empty_frames = cfg.auto_background_empty_frames;
    out.auto_background_cooldown_frames = cfg.auto_background_cooldown_frames;
    out.enable_target_group = cfg.enable_target_group;
    out.target_group_area_min = cfg.target_group_area_min;
    out.target_group_area_max = cfg.target_group_area_max;
    out.target_group_deformability_min = cfg.target_group_deformability_min;
    out.target_group_deformability_max = cfg.target_group_deformability_max;
    out.enable_target_group_emodulus = cfg.enable_target_group_emodulus;
    out.target_group_emodulus_min = cfg.target_group_emodulus_min;
    out.target_group_emodulus_max = cfg.target_group_emodulus_max;
    out.multi_image_enabled = cfg.multi_image_enabled;
    out.multi_image_count = cfg.multi_image_count;
    return out;
}

backend::services::ProcessingConfig merge_processing_config(
    backend::services::ProcessingConfig current,
    const BridgeProcessingConfig& input) {
    current.gaussian_blur_size = input.gaussian_blur_size;
    current.bg_subtract_threshold = input.bg_subtract_threshold;
    current.morph_kernel_size = input.morph_kernel_size;
    current.morph_iterations = input.morph_iterations;
    current.area_threshold_min = input.area_threshold_min;
    current.area_threshold_max = input.area_threshold_max;
    current.deformability_threshold_min = input.deformability_threshold_min;
    current.deformability_threshold_max = input.deformability_threshold_max;
    current.enable_border_check = input.enable_border_check;
    current.enable_area_range_check = input.enable_area_range_check;
    current.enable_deformability_range_check = input.enable_deformability_range_check;
    current.area_ratio_threshold_max = input.area_ratio_threshold_max;
    current.enable_area_ratio_check = input.enable_area_ratio_check;
    current.require_single_inner_contour = input.require_single_inner_contour;
    current.empty_frame_pixel_threshold = input.empty_frame_pixel_threshold;
    current.auto_background_enabled = input.auto_background_enabled;
    current.auto_background_empty_frames = input.auto_background_empty_frames;
    current.auto_background_cooldown_frames = input.auto_background_cooldown_frames;
    current.enable_target_group = input.enable_target_group;
    current.target_group_area_min = input.target_group_area_min;
    current.target_group_area_max = input.target_group_area_max;
    current.target_group_deformability_min = input.target_group_deformability_min;
    current.target_group_deformability_max = input.target_group_deformability_max;
    current.enable_target_group_emodulus = input.enable_target_group_emodulus;
    current.target_group_emodulus_min = input.target_group_emodulus_min;
    current.target_group_emodulus_max = input.target_group_emodulus_max;
    current.multi_image_enabled = input.multi_image_enabled;
    current.multi_image_count = input.multi_image_count;
    return current;
}

BridgeBrightnessQuantiles to_bridge_brightness(const backend::services::BrightnessQuantiles& b) {
    BridgeBrightnessQuantiles out{};
    out.q1 = b.q1;
    out.q2 = b.q2;
    out.q3 = b.q3;
    out.q4 = b.q4;
    return out;
}

BridgeFilterResult to_bridge_filter(const backend::services::FilterResult& r) {
    BridgeFilterResult out{};
    out.is_valid = r.isValid;
    out.touches_border = r.touchesBorder;
    out.has_single_inner_contour = r.hasSingleInnerContour;
    out.in_range = r.inRange;
    out.inner_contour_count = r.innerContourCount;
    out.deformability = r.deformability;
    out.area = r.area;
    out.area_ratio = r.areaRatio;
    out.ring_ratio = r.ringRatio;
    out.youngs_modulus = r.youngsModulus;
    out.brightness = to_bridge_brightness(r.brightness);
    out.is_target_group = r.isTargetGroup;
    return out;
}

BridgeProcessedFrame to_bridge_processed_frame(const backend::services::ProcessedFrame& frame) {
    BridgeProcessedFrame out{};
    out.index = frame.index;
    out.timestamp_ns = frame.timestampNs;
    out.validation = to_bridge_filter(frame.validation);

    const cv::Mat& image = !frame.originalImage.empty() ? frame.originalImage : frame.processedImage;
    if (!image.empty()) {
        const auto png = encode_mat_png(image);
        out.image_base64 = rust::String(base64_encode(png.data(), png.size()));
        out.image_width = static_cast<std::uint32_t>(image.cols);
        out.image_height = static_cast<std::uint32_t>(image.rows);
    }
    return out;
}

std::optional<backend::services::SyringePumpService::PumpId> to_pump_id(std::int32_t id) {
    using PumpId = backend::services::SyringePumpService::PumpId;
    if (id == static_cast<std::int32_t>(PumpId::Sample)) {
        return PumpId::Sample;
    }
    if (id == static_cast<std::int32_t>(PumpId::Sheath)) {
        return PumpId::Sheath;
    }
    return std::nullopt;
}

std::optional<backend::services::SyringePumpService::Direction> parse_direction(std::string value) {
    using Direction = backend::services::SyringePumpService::Direction;
    value = to_lower(std::move(value));
    if (value == "infuse") {
        return Direction::Infuse;
    }
    if (value == "withdraw") {
        return Direction::Withdraw;
    }
    return std::nullopt;
}

rust::String run_status_to_string(backend::services::SyringePumpService::RunStatus status) {
    using RunStatus = backend::services::SyringePumpService::RunStatus;
    switch (status) {
    case RunStatus::Stop:
        return rust::String("stop");
    case RunStatus::Forward:
        return rust::String("forward");
    case RunStatus::Backward:
        return rust::String("backward");
    case RunStatus::Pause:
        return rust::String("pause");
    default:
        return rust::String("stop");
    }
}

rust::String pump_direction_to_string(backend::services::SyringePumpService::Direction direction) {
    using Direction = backend::services::SyringePumpService::Direction;
    return rust::String(direction == Direction::Infuse ? "infuse" : "withdraw");
}

BridgeAutofocusConfig to_bridge_autofocus_config(const backend::services::AutofocusService::Config& cfg) {
    BridgeAutofocusConfig out{};
    out.focus_setpoint = cfg.focusSetpoint;
    out.focus_range = cfg.focusRange;
    out.voltage_step = cfg.voltageStep;
    out.fine_voltage_step = cfg.fineVoltageStep;
    out.max_voltage = cfg.maxVoltage;
    out.min_voltage = cfg.minVoltage;
    out.initial_voltage = cfg.initialVoltage;
    out.manual_voltage_step = cfg.manualVoltageStep;
    out.ring_ratio_stale_ms = cfg.ringRatioStaleMs;
    out.require_new_sample_per_step = cfg.requireNewSamplePerStep;
    out.min_samples_per_step = cfg.minSamplesPerStep;
    out.safe_shutdown_voltage = cfg.safeShutdownVoltage;
    out.focus_direction = cfg.focusDirection;
    return out;
}

backend::services::AutofocusService::Config from_bridge_autofocus_config(const BridgeAutofocusConfig& cfg) {
    backend::services::AutofocusService::Config out{};
    out.focusSetpoint = cfg.focus_setpoint;
    out.focusRange = cfg.focus_range;
    out.voltageStep = cfg.voltage_step;
    out.fineVoltageStep = cfg.fine_voltage_step;
    out.maxVoltage = cfg.max_voltage;
    out.minVoltage = cfg.min_voltage;
    out.initialVoltage = cfg.initial_voltage;
    out.manualVoltageStep = cfg.manual_voltage_step;
    out.ringRatioStaleMs = cfg.ring_ratio_stale_ms;
    out.requireNewSamplePerStep = cfg.require_new_sample_per_step;
    out.minSamplesPerStep = cfg.min_samples_per_step;
    out.safeShutdownVoltage = cfg.safe_shutdown_voltage;
    out.focusDirection = cfg.focus_direction;
    return out;
}

BridgePumpStatus to_bridge_pump_status(const backend::services::SyringePumpService::PumpStatus& status) {
    BridgePumpStatus out{};
    out.connected = status.connected;
    out.run_status = run_status_to_string(status.runStatus);
    out.current_flow_rate = status.currentFlowRate;
    out.accumulated_volume = status.accumulatedVolume;
    out.min_flow_rate = status.minFlowRate;
    out.max_flow_rate = status.maxFlowRate;
    out.stalled = status.stalled;
    return out;
}

BridgePumpConfig to_bridge_pump_config(const backend::services::SyringePumpService::PumpConfig& cfg) {
    BridgePumpConfig out{};
    out.com_port = cfg.comPort;
    out.baud_rate = cfg.baudRate;
    out.modbus_address = cfg.modbusAddress;
    out.flow_rate = cfg.flowRate;
    out.flow_rate_unit = cfg.flowRateUnit;
    out.direction = pump_direction_to_string(cfg.direction);
    return out;
}

rust::String export_frames_to_csv(const std::vector<backend::services::ProcessedFrame>& valid_frames,
                                  const std::vector<backend::services::ProcessedFrame>& invalid_frames,
                                  double pixel_to_micron_factor,
                                  const std::string& output_path) {
    std::ofstream out(output_path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return rust::String("Failed to open output CSV file");
    }

    const double area_factor = pixel_to_micron_factor * pixel_to_micron_factor;
    out << "Frame Type,Index,Timestamp,Deformability,Area,Area (um^2),Area Ratio,Ring Ratio,"
        << "Valid,Touches Border,Single Inner,In Range,Inner Count,"
        << "Bright Q1,Bright Q2,Bright Q3,Bright Q4\n";

    auto write_rows = [&](const std::vector<backend::services::ProcessedFrame>& frames,
                          const char* label) {
        for (const auto& frame : frames) {
            const auto& v = frame.validation;
            out << label << ','
                << frame.index << ','
                << frame.timestampNs << ','
                << std::fixed << std::setprecision(3) << v.deformability << ','
                << std::fixed << std::setprecision(2) << v.area << ','
                << std::fixed << std::setprecision(2) << (v.area * area_factor) << ','
                << std::fixed << std::setprecision(3) << v.areaRatio << ','
                << std::fixed << std::setprecision(3) << v.ringRatio << ','
                << (v.isValid ? "Yes" : "No") << ','
                << (v.touchesBorder ? "Yes" : "No") << ','
                << (v.hasSingleInnerContour ? "Yes" : "No") << ','
                << (v.inRange ? "Yes" : "No") << ','
                << v.innerContourCount << ','
                << std::fixed << std::setprecision(2) << v.brightness.q1 << ','
                << std::fixed << std::setprecision(2) << v.brightness.q2 << ','
                << std::fixed << std::setprecision(2) << v.brightness.q3 << ','
                << std::fixed << std::setprecision(2) << v.brightness.q4 << '\n';
        }
    };

    write_rows(valid_frames, "Valid");
    write_rows(invalid_frames, "Invalid");

    if (!out.good()) {
        return rust::String("Failed while writing CSV output");
    }

    return rust::String();
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
        backend_->setBackgroundCaptureCallback({});
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
    const auto store = backend_->getFrameStore();
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

    backend_->setBackgroundCaptureCallback([](const cv::Mat& bg, std::uint64_t frameIndex) {
        if (bg.empty()) {
            return;
        }
        mib_emit_background(frameIndex, vec_to_rust(encode_mat_png(bg)));
    });

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
    shim.app_backend().setHardwareCameraSelection(interface_index, device_index, to_std_string(label));
}

rust::String bridge_configure_mock(const AppBackendShim& shim,
                                   rust::Str dir,
                                   std::uint32_t interval_ms,
                                   bool loop_files) {
    if (!shim.app_backend_ptr()) {
        return rust::String("Backend is not initialized");
    }
    camera::mock::MockCameraOptions options;
    options.folder = std::filesystem::path(to_std_string(dir));
    if (interval_ms == 0) {
        return rust::String("Frame interval must be >= 1 ms");
    }
    options.frameInterval = std::chrono::milliseconds(interval_ms);
    options.loopFiles = loop_files;

    std::string validationError;
    if (!camera::mock::validateMockImageFolder(options.folder, validationError, true)) {
        return rust::String(validationError);
    }
    shim.app_backend().configureMockCamera(options);
    return rust::String();
}

bool bridge_start_capture(const AppBackendShim& shim) {
    return shim.app_backend_ptr() && shim.app_backend().capture().start();
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
    if (!shim.app_backend_ptr()) {
        return {};
    }
    backend::playback::Frame f;
    if (!shim.app_backend().playback().fetchLatest(f) || f.data.empty()) {
        return {};
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

rust::Vec<std::uint8_t> bridge_fetch_frame_by_index_png(const AppBackendShim& shim, std::uint64_t index) {
    if (!shim.app_backend_ptr()) {
        return {};
    }
    backend::playback::Frame f;
    if (!shim.app_backend().playback().fetchByIndex(index, f) || f.data.empty()) {
        return {};
    }
    return encode_frame_png(f.data.data(),
                            f.data.size(),
                            f.width,
                            f.height,
                            f.linePitch,
                            f.pixelFormat);
}

BridgeFrameMeta bridge_fetch_frame_by_index_meta(const AppBackendShim& shim, std::uint64_t index) {
    BridgeFrameMeta m{};
    if (!shim.app_backend_ptr()) {
        return m;
    }
    backend::playback::Frame f;
    if (!shim.app_backend().playback().fetchByIndex(index, f) || f.data.empty()) {
        return m;
    }
    m.index = index;
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

BridgeProcessingConfig bridge_get_processing_config(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return {};
    }
    return to_bridge_processing_config(shim.app_backend().processing().getProcessingConfig());
}

void bridge_set_processing_config(const AppBackendShim& shim, const BridgeProcessingConfig& config) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    auto& processing = shim.app_backend().processing();
    auto merged = merge_processing_config(processing.getProcessingConfig(), config);
    processing.setProcessingConfig(merged);
}

void bridge_set_realtime_roi(const AppBackendShim& shim, const BridgeRoi& roi) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    backend::services::ProcessingService::Roi mapped{};
    mapped.x = roi.x;
    mapped.y = roi.y;
    mapped.w = roi.w;
    mapped.h = roi.h;
    shim.app_backend().processing().setRealtimeRoi(mapped);
}

void bridge_clear_realtime_roi(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().processing().setRealtimeRoi({0, 0, 0, 0});
}

bool bridge_set_realtime_background(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return false;
    }
    backend::playback::Frame frame{};
    if (!shim.app_backend().playback().fetchLatest(frame) || frame.data.empty() || frame.width == 0 || frame.height == 0) {
        return false;
    }

    cv::Mat gray;
    if (frame.pixelFormat == kPfncMono8 || frame.pixelFormat == 0) {
        const int w = static_cast<int>(frame.width);
        const int h = static_cast<int>(frame.height);
        const int step = frame.linePitch > 0 ? static_cast<int>(frame.linePitch) : w;
        cv::Mat mono(h, w, CV_8UC1, frame.data.data(), step);
        gray = mono.clone();
    } else if (frame.data.size() == frame.width * frame.height * 3) {
        cv::Mat bgr(static_cast<int>(frame.height), static_cast<int>(frame.width), CV_8UC3, frame.data.data());
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    } else {
        return false;
    }

    shim.app_backend().processing().setRealtimeBackgroundGray(gray);
    return true;
}

BridgeMonitoringFrames bridge_get_monitoring_frames(const AppBackendShim& shim) {
    BridgeMonitoringFrames out{};
    if (!shim.app_backend_ptr()) {
        return out;
    }
    auto valid = shim.app_backend().processing().getMonitoringValidFrames();
    auto invalid = shim.app_backend().processing().getMonitoringInvalidFrames();
    out.valid.reserve(valid.size());
    out.invalid.reserve(invalid.size());
    for (const auto& frame : valid) {
        out.valid.push_back(to_bridge_processed_frame(frame));
    }
    for (const auto& frame : invalid) {
        out.invalid.push_back(to_bridge_processed_frame(frame));
    }
    return out;
}

void bridge_clear_monitoring_frames(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().processing().clearMonitoringFrames();
}

rust::String bridge_start_experiment(const AppBackendShim& shim, rust::Str hdf5_path) {
    if (!shim.app_backend_ptr()) {
        return rust::String("Backend is not initialized");
    }
    if (shim.experiment_active_.load(std::memory_order_relaxed)) {
        return rust::String("Experiment is already running");
    }
    if (!shim.app_backend().capture().isRunning()) {
        return rust::String("Camera must be running before starting an experiment");
    }

    const std::string path = ensure_hdf5_extension(to_std_string(hdf5_path));
    auto& hdf5 = shim.app_backend().hdf5();
    if (!hdf5.openFile(path)) {
        return rust::String("Failed to open HDF5 file");
    }
    if (!hdf5.initializeDatasets()) {
        SPDLOG_WARN("Failed to initialize HDF5 datasets for experiment");
    }

    shim.app_backend().processing().startExperiment();
    shim.experiment_start_time_ns_.store(now_unix_ns(), std::memory_order_relaxed);
    shim.experiment_active_.store(true, std::memory_order_relaxed);
    return rust::String();
}

rust::String bridge_stop_experiment(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return rust::String("Backend is not initialized");
    }
    if (!shim.experiment_active_.load(std::memory_order_relaxed)) {
        return rust::String("No experiment is currently running");
    }

    auto& backend = shim.app_backend();
    auto& processing = backend.processing();
    auto& hdf5 = backend.hdf5();

    if (hdf5.isFileOpen()) {
        processing.flushBufferedFrames(hdf5);
        auto valid_frames = processing.getValidFrames();
        auto invalid_frames = processing.getInvalidFrames();
        if (!valid_frames.empty() || !invalid_frames.empty()) {
            if (!hdf5.appendFrames(valid_frames, invalid_frames)) {
                SPDLOG_WARN("Failed to append remaining experiment frames");
            }
        }

        const std::uint64_t end_ns = now_unix_ns();
        const std::uint64_t start_ns = shim.experiment_start_time_ns_.load(std::memory_order_relaxed);
        if (!hdf5.flush()) {
            SPDLOG_WARN("H5Fflush before writeExperimentInfo failed");
        }
        const auto processing_config = processing.getProcessingConfig();
        const auto roi = processing.getRealtimeRoi();
        const cv::Mat bg = processing.getRealtimeBackgroundGray();
        hdf5.writeExperimentInfo(start_ns,
                                 end_ns,
                                 valid_frames.size(),
                                 invalid_frames.size(),
                                 processing_config,
                                 roi,
                                 bg.empty() ? nullptr : &bg);

        const std::string config_json = backend.getLastConfigJson();
        if (!config_json.empty()) {
            hdf5.writeConfigJson(config_json);
        }
        hdf5.closeFile();
    }

    processing.endExperiment();
    processing.resetRealtimeMetrics();
    shim.experiment_active_.store(false, std::memory_order_relaxed);
    return rust::String();
}

rust::String bridge_load_hdf5_file(const AppBackendShim& shim, rust::Str path) {
    if (!shim.app_backend_ptr()) {
        return rust::String("Backend is not initialized");
    }
    auto& hdf5 = shim.app_backend().hdf5();
    if (hdf5.isFileOpen()) {
        hdf5.closeFile();
    }
    if (!hdf5.loadFile(to_std_string(path))) {
        return rust::String("Failed to load HDF5 file");
    }
    return rust::String();
}

rust::Vec<BridgeProcessedFrame> bridge_get_hdf5_valid_frames(const AppBackendShim& shim) {
    rust::Vec<BridgeProcessedFrame> out;
    if (!shim.app_backend_ptr()) {
        return out;
    }
    std::vector<backend::services::ProcessedFrame> frames;
    if (!shim.app_backend().hdf5().readValidFrames(frames)) {
        return out;
    }
    out.reserve(frames.size());
    for (const auto& frame : frames) {
        out.push_back(to_bridge_processed_frame(frame));
    }
    return out;
}

rust::Vec<BridgeProcessedFrame> bridge_get_hdf5_invalid_frames(const AppBackendShim& shim) {
    rust::Vec<BridgeProcessedFrame> out;
    if (!shim.app_backend_ptr()) {
        return out;
    }
    std::vector<backend::services::ProcessedFrame> frames;
    if (!shim.app_backend().hdf5().readInvalidFrames(frames)) {
        return out;
    }
    out.reserve(frames.size());
    for (const auto& frame : frames) {
        out.push_back(to_bridge_processed_frame(frame));
    }
    return out;
}

rust::String bridge_export_metrics_csv(const AppBackendShim& shim, rust::Str hdf5_path, rust::Str output_path) {
    if (!shim.app_backend_ptr()) {
        return rust::String("Backend is not initialized");
    }

    const std::string csv_path = to_std_string(output_path);
    if (csv_path.empty()) {
        return rust::String("Output CSV path is required");
    }

    auto& backend = shim.app_backend();
    auto& hdf5 = backend.hdf5();

    const std::string requested_hdf5 = to_std_string(hdf5_path);
    bool close_after = false;
    if (!requested_hdf5.empty()) {
        if (hdf5.isFileOpen()) {
            hdf5.closeFile();
        }
        if (!hdf5.loadFile(requested_hdf5)) {
            return rust::String("Failed to load HDF5 file for CSV export");
        }
        close_after = true;
    } else if (!hdf5.isFileOpen()) {
        return rust::String("HDF5 file is not open");
    }

    std::vector<backend::services::ProcessedFrame> valid_frames;
    std::vector<backend::services::ProcessedFrame> invalid_frames;
    if (!hdf5.readValidMetadata(valid_frames) || !hdf5.readInvalidMetadata(invalid_frames)) {
        if (close_after) {
            hdf5.closeFile();
        }
        return rust::String("Failed to read HDF5 metadata for CSV export");
    }

    const rust::String err = export_frames_to_csv(
        valid_frames,
        invalid_frames,
        backend.processing().getPixelToMicronFactor(),
        csv_path);

    if (close_after) {
        hdf5.closeFile();
    }

    return err;
}

bool bridge_start_frame_recording(const AppBackendShim& shim, rust::Str hdf5_path) {
    if (!shim.app_backend_ptr()) {
        return false;
    }
    return shim.app_backend().startFrameRecording(ensure_hdf5_extension(to_std_string(hdf5_path)));
}

void bridge_stop_frame_recording(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().stopFrameRecording();
}

bool bridge_connect_autofocus(const AppBackendShim& shim,
                              std::int32_t com_port,
                              std::int32_t baud_rate,
                              std::uint8_t device_address) {
    if (!shim.app_backend_ptr()) {
        return false;
    }
    return shim.app_backend().autofocus().connect(com_port, baud_rate, device_address);
}

void bridge_disconnect_autofocus(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().autofocus().disconnect();
}

void bridge_set_autofocus_enabled(const AppBackendShim& shim, bool enabled) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().autofocus().setEnabled(enabled);
}

void bridge_increase_voltage(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().autofocus().increaseVoltage();
}

void bridge_decrease_voltage(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().autofocus().decreaseVoltage();
}

BridgeAutofocusConfig bridge_get_autofocus_config(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return {};
    }
    return to_bridge_autofocus_config(shim.app_backend().autofocus().getConfig());
}

void bridge_set_autofocus_config(const AppBackendShim& shim, const BridgeAutofocusConfig& config) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().autofocus().setConfig(from_bridge_autofocus_config(config));
}

bool bridge_connect_pump(const AppBackendShim& shim,
                         std::int32_t pump_id,
                         std::int32_t com_port,
                         std::int32_t baud_rate,
                         std::uint8_t modbus_address) {
    if (!shim.app_backend_ptr()) {
        return false;
    }
    const auto id = to_pump_id(pump_id);
    if (!id.has_value()) {
        return false;
    }
    return shim.app_backend().syringePump().connect(*id, com_port, baud_rate, modbus_address);
}

void bridge_disconnect_pump(const AppBackendShim& shim, std::int32_t pump_id) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    const auto id = to_pump_id(pump_id);
    if (!id.has_value()) {
        return;
    }
    shim.app_backend().syringePump().disconnect(*id);
}

bool bridge_set_pump_flow_rate(const AppBackendShim& shim, std::int32_t pump_id, double rate, std::uint16_t unit) {
    if (!shim.app_backend_ptr()) {
        return false;
    }
    const auto id = to_pump_id(pump_id);
    if (!id.has_value()) {
        return false;
    }
    return shim.app_backend().syringePump().setFlowRate(*id, rate, unit);
}

bool bridge_set_pump_direction(const AppBackendShim& shim, std::int32_t pump_id, rust::Str direction) {
    if (!shim.app_backend_ptr()) {
        return false;
    }
    const auto id = to_pump_id(pump_id);
    const auto dir = parse_direction(to_std_string(direction));
    if (!id.has_value() || !dir.has_value()) {
        return false;
    }
    return shim.app_backend().syringePump().setDirection(*id, *dir);
}

bool bridge_start_pump(const AppBackendShim& shim, std::int32_t pump_id) {
    if (!shim.app_backend_ptr()) {
        return false;
    }
    const auto id = to_pump_id(pump_id);
    if (!id.has_value()) {
        return false;
    }
    return shim.app_backend().syringePump().start(*id);
}

bool bridge_stop_pump(const AppBackendShim& shim, std::int32_t pump_id) {
    if (!shim.app_backend_ptr()) {
        return false;
    }
    const auto id = to_pump_id(pump_id);
    if (!id.has_value()) {
        return false;
    }
    return shim.app_backend().syringePump().stop(*id);
}

bool bridge_purge_pump(const AppBackendShim& shim, std::int32_t pump_id, rust::Str direction) {
    if (!shim.app_backend_ptr()) {
        return false;
    }
    const auto id = to_pump_id(pump_id);
    const auto dir = parse_direction(to_std_string(direction));
    if (!id.has_value() || !dir.has_value()) {
        return false;
    }
    return shim.app_backend().syringePump().purge(*id, *dir);
}

BridgePumpStatus bridge_get_pump_status(const AppBackendShim& shim, std::int32_t pump_id) {
    if (!shim.app_backend_ptr()) {
        return {};
    }
    const auto id = to_pump_id(pump_id);
    if (!id.has_value()) {
        return {};
    }
    shim.app_backend().syringePump().pollStatus(*id);
    return to_bridge_pump_status(shim.app_backend().syringePump().getStatus(*id));
}

BridgePumpConfig bridge_get_pump_config(const AppBackendShim& shim, std::int32_t pump_id) {
    if (!shim.app_backend_ptr()) {
        return {};
    }
    const auto id = to_pump_id(pump_id);
    if (!id.has_value()) {
        return {};
    }
    return to_bridge_pump_config(shim.app_backend().syringePump().getConfig(*id));
}

void bridge_fire_sort_trigger(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().trigger().onTargetGroupResult(true);
}

void bridge_set_trigger_duration(const AppBackendShim& shim, std::int32_t duration_us) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().trigger().setPulseDurationUs(std::max(duration_us, 1));
}

rust::String bridge_get_app_config(const AppBackendShim& shim) {
    if (!shim.app_backend_ptr()) {
        return rust::String("{}");
    }
    const std::string json = shim.app_backend().getLastConfigJson();
    return rust::String(json.empty() ? "{}" : json);
}

void bridge_set_app_config(const AppBackendShim& shim, rust::Str json) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().setLastConfigJson(to_std_string(json));
}

rust::String bridge_apply_camera_script(const AppBackendShim& shim, rust::Str path) {
    if (!shim.app_backend_ptr()) {
        return rust::String("Backend is not initialized");
    }
    std::string error;
    if (shim.app_backend().applyCameraScriptFromFile(to_std_string(path), &error)) {
        return rust::String();
    }
    if (error.empty()) {
        error = "Failed to apply camera script";
    }
    return rust::String(error);
}

void bridge_set_pixel_to_micron_factor(const AppBackendShim& shim, double factor) {
    if (!shim.app_backend_ptr()) {
        return;
    }
    shim.app_backend().processing().setPixelToMicronFactor(factor);
}

rust::String bridge_save_buffer_to_disk(const AppBackendShim& shim,
                                        rust::Str output_dir,
                                        std::uint64_t start_index,
                                        std::uint64_t end_index,
                                        bool use_range) {
    if (!shim.app_backend_ptr()) {
        return rust::String("Backend is not initialized");
    }
    const std::string out_dir = to_std_string(output_dir);
    if (out_dir.empty()) {
        return rust::String("Output directory is required");
    }
    const bool ok = use_range
                        ? shim.app_backend().playback().saveFramesToDisk(out_dir, start_index, end_index)
                        : shim.app_backend().playback().saveFramesToDisk(out_dir);
    if (!ok) {
        return rust::String("Failed to save playback buffer to disk");
    }
    return rust::String();
}

void bridge_install_emitters(AppBackendShim& shim) {
    shim.install_emitters_impl();
}

std::unique_ptr<AppBackendShim> create_shim(rust::Str data_dir) {
    auto backend = std::make_unique<backend::AppBackend>();
    const std::string dir = to_std_string(data_dir);
    if (!backend->initialize(dir)) {
        return nullptr;
    }
    return std::make_unique<AppBackendShim>(std::move(backend));
}
