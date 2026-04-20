#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "rust/cxx.h"

namespace backend {
class AppBackend;
}

/// Opaque handle; implementation in bridge.cpp (pimpl-style layout, no cxx shared types in this header).
class AppBackendShim {
public:
    explicit AppBackendShim(std::unique_ptr<backend::AppBackend> backend);
    ~AppBackendShim();

    rust::String backend_version() const;

    bool is_experiment_active() const {
        return experiment_active_.load(std::memory_order_relaxed);
    }
    void set_experiment_active(bool active) {
        experiment_active_.store(active, std::memory_order_relaxed);
    }
    std::uint64_t experiment_start_time_ns() const {
        return experiment_start_time_ns_.load(std::memory_order_relaxed);
    }
    void set_experiment_start_time_ns(std::uint64_t ts_ns) {
        experiment_start_time_ns_.store(ts_ns, std::memory_order_relaxed);
    }

    /// Used by cxx free functions in bridge.cpp.
    backend::AppBackend& app_backend() const { return *backend_; }
    backend::AppBackend* app_backend_ptr() const { return backend_.get(); }

    void install_emitters_impl();
    bool experiment_active() const { return experiment_active_.load(std::memory_order_relaxed); }
    void set_experiment_active(bool active) const { experiment_active_.store(active, std::memory_order_relaxed); }
    std::uint64_t experiment_start_time_ns() const {
        return experiment_start_time_ns_.load(std::memory_order_relaxed);
    }
    void set_experiment_start_time_ns(std::uint64_t value) const {
        experiment_start_time_ns_.store(value, std::memory_order_relaxed);
    }
    bool is_experiment_active() const {
        return experiment_active_.load(std::memory_order_relaxed);
    }
    std::uint64_t experiment_start_time_ns() const {
        return experiment_start_time_ns_.load(std::memory_order_relaxed);
    }
    void mark_experiment_started(std::uint64_t start_time_ns) {
        experiment_start_time_ns_.store(start_time_ns, std::memory_order_relaxed);
        experiment_active_.store(true, std::memory_order_relaxed);
    }
    void clear_experiment_state() {
        experiment_active_.store(false, std::memory_order_relaxed);
        experiment_start_time_ns_.store(0, std::memory_order_relaxed);
    }

private:
    void on_frame(const std::uint8_t* data,
                  std::size_t size,
                  std::uint64_t width,
                  std::uint64_t height,
                  std::size_t line_pitch,
                  std::uint64_t pixel_format,
                  std::uint64_t timestamp_ns);

    void stats_loop();

    std::unique_ptr<backend::AppBackend> backend_;
    std::thread stats_thread_;
    std::atomic<bool> stats_stop_{false};
    std::atomic<bool> emitters_installed_{false};
    mutable std::atomic<bool> experiment_active_{false};
    mutable std::atomic<std::uint64_t> experiment_start_time_ns_{0};
};

std::unique_ptr<AppBackendShim> create_shim(rust::Str data_dir);

// Shared POD layouts must match src-tauri/src/bridge/ffi.rs (cxx-generated header uses these guards).
#ifndef CXXBRIDGE1_STRUCT_BridgeCamera
#define CXXBRIDGE1_STRUCT_BridgeCamera
struct BridgeCamera final {
    std::int32_t interface_index = 0;
    std::int32_t device_index = 0;
    rust::String interface_id;
    rust::String device_id;
    rust::String model_name;
    rust::String firmware_version;
    rust::String label;
};
#endif

#ifndef CXXBRIDGE1_STRUCT_BridgeFramegrabber
#define CXXBRIDGE1_STRUCT_BridgeFramegrabber
struct BridgeFramegrabber final {
    std::int32_t interface_index = 0;
    std::int32_t device_index = 0;
    std::int32_t stream_index = 0;
    rust::String interface_id;
    rust::String device_id;
    rust::String stream_id;
    rust::String model_name;
    rust::String label;
};
#endif

#ifndef CXXBRIDGE1_STRUCT_BridgePlaybackRange
#define CXXBRIDGE1_STRUCT_BridgePlaybackRange
struct BridgePlaybackRange final {
    std::uint64_t earliest = 0;
    std::uint64_t latest = 0;
    std::uint64_t count = 0;
};
#endif

#ifndef CXXBRIDGE1_STRUCT_BridgeFrameMeta
#define CXXBRIDGE1_STRUCT_BridgeFrameMeta
struct BridgeFrameMeta final {
    std::uint64_t index = 0;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    std::uint64_t timestamp_ns = 0;
};
#endif

#ifndef CXXBRIDGE1_STRUCT_BridgeRoi
#define CXXBRIDGE1_STRUCT_BridgeRoi
struct BridgeRoi final {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t w = 0;
    std::int32_t h = 0;
};
#endif

#ifndef CXXBRIDGE1_STRUCT_BridgeProcessingConfig
#define CXXBRIDGE1_STRUCT_BridgeProcessingConfig
struct BridgeProcessingConfig final {
    std::int32_t gaussian_blur_size = 3;
    std::int32_t bg_subtract_threshold = 8;
    std::int32_t morph_kernel_size = 3;
    std::int32_t morph_iterations = 1;
    std::int32_t area_threshold_min = 60;
    std::int32_t area_threshold_max = 290;
    double deformability_threshold_min = 0.0;
    double deformability_threshold_max = 1.0;
    bool enable_border_check = true;
    bool enable_area_range_check = true;
    bool enable_deformability_range_check = false;
    double area_ratio_threshold_max = 1.5;
    bool enable_area_ratio_check = false;
    double ring_ratio_min = 15.0;
    double ring_ratio_max = 25.0;
    bool enable_ring_ratio_check = true;
    bool require_single_inner_contour = true;
    std::int32_t empty_frame_pixel_threshold = 100;
    bool auto_background_enabled = false;
    std::int32_t auto_background_empty_frames = 30;
    std::int32_t auto_background_cooldown_frames = 1000;
    bool enable_target_group = false;
    std::int32_t target_group_area_min = 72;
    std::int32_t target_group_area_max = 191;
    double target_group_deformability_min = 0.0;
    double target_group_deformability_max = 0.3;
    bool enable_target_group_emodulus = false;
    double target_group_emodulus_min = 0.0;
    double target_group_emodulus_max = 10.0;
    bool multi_image_enabled = false;
    std::int32_t multi_image_count = 1;
};
#endif

#ifndef CXXBRIDGE1_STRUCT_BridgeBrightnessQuantiles
#define CXXBRIDGE1_STRUCT_BridgeBrightnessQuantiles
struct BridgeBrightnessQuantiles final {
    double q1 = 0.0;
    double q2 = 0.0;
    double q3 = 0.0;
    double q4 = 0.0;
};
#endif

#ifndef CXXBRIDGE1_STRUCT_BridgeFilterResult
#define CXXBRIDGE1_STRUCT_BridgeFilterResult
struct BridgeFilterResult final {
    bool is_valid = false;
    bool touches_border = false;
    bool has_single_inner_contour = false;
    bool in_range = false;
    std::int32_t inner_contour_count = 0;
    double deformability = 0.0;
    double area = 0.0;
    double area_ratio = 0.0;
    double ring_ratio = 0.0;
    double youngs_modulus = 0.0;
    BridgeBrightnessQuantiles brightness{};
    bool is_target_group = false;
};
#endif

#ifndef CXXBRIDGE1_STRUCT_BridgeProcessedFrame
#define CXXBRIDGE1_STRUCT_BridgeProcessedFrame
struct BridgeProcessedFrame final {
    std::uint64_t index = 0;
    std::uint64_t timestamp_ns = 0;
    rust::String image_base64;
    std::uint32_t image_width = 0;
    std::uint32_t image_height = 0;
    BridgeFilterResult validation{};
};
#endif

#ifndef CXXBRIDGE1_STRUCT_BridgeMonitoringFrames
#define CXXBRIDGE1_STRUCT_BridgeMonitoringFrames
struct BridgeMonitoringFrames final {
    rust::Vec<BridgeProcessedFrame> valid;
    rust::Vec<BridgeProcessedFrame> invalid;
};
#endif

#ifndef CXXBRIDGE1_STRUCT_BridgeAutofocusConfig
#define CXXBRIDGE1_STRUCT_BridgeAutofocusConfig
struct BridgeAutofocusConfig final {
    double focus_setpoint = 20.0;
    double focus_range = 0.5;
    double voltage_step = 1.0;
    double fine_voltage_step = 0.2;
    double max_voltage = 100.0;
    double min_voltage = 0.0;
    double initial_voltage = 50.0;
    double manual_voltage_step = 1.0;
    std::int32_t ring_ratio_stale_ms = 1500;
    bool require_new_sample_per_step = true;
    std::int32_t min_samples_per_step = 100;
    double safe_shutdown_voltage = 0.0;
    bool focus_direction = true;
};
#endif

#ifndef CXXBRIDGE1_STRUCT_BridgePumpStatus
#define CXXBRIDGE1_STRUCT_BridgePumpStatus
struct BridgePumpStatus final {
    bool connected = false;
    rust::String run_status;
    double current_flow_rate = 0.0;
    double accumulated_volume = 0.0;
    double min_flow_rate = 0.0;
    double max_flow_rate = 0.0;
    bool stalled = false;
};
#endif

#ifndef CXXBRIDGE1_STRUCT_BridgePumpConfig
#define CXXBRIDGE1_STRUCT_BridgePumpConfig
struct BridgePumpConfig final {
    std::int32_t com_port = -1;
    std::int32_t baud_rate = 115200;
    std::uint8_t modbus_address = 1;
    double flow_rate = 0.0;
    std::uint16_t flow_rate_unit = 100;
    rust::String direction;
};
#endif

// cxx free functions (declarations required before ffi.rs.cc); definitions in bridge.cpp.
rust::Vec<BridgeCamera> bridge_discover_cameras(const AppBackendShim& shim);
rust::Vec<BridgeFramegrabber> bridge_discover_framegrabbers(const AppBackendShim& shim);
void bridge_set_hardware_camera(const AppBackendShim& shim,
                                std::int32_t interface_index,
                                std::int32_t device_index,
                                rust::Str label);
rust::String bridge_configure_mock(const AppBackendShim& shim,
                                   rust::Str dir,
                                   std::uint32_t interval_ms,
                                   bool loop_files);
bool bridge_start_capture(const AppBackendShim& shim);
void bridge_stop_capture(const AppBackendShim& shim);
bool bridge_is_capture_running(const AppBackendShim& shim);
rust::Vec<std::uint8_t> bridge_fetch_latest_frame_png(const AppBackendShim& shim);
BridgeFrameMeta bridge_fetch_latest_frame_meta(const AppBackendShim& shim);
rust::Vec<std::uint8_t> bridge_fetch_frame_by_index_png(const AppBackendShim& shim, std::uint64_t index);
BridgeFrameMeta bridge_fetch_frame_by_index_meta(const AppBackendShim& shim, std::uint64_t index);
BridgePlaybackRange bridge_get_playback_range(const AppBackendShim& shim);

BridgeProcessingConfig bridge_get_processing_config(const AppBackendShim& shim);
void bridge_set_processing_config(const AppBackendShim& shim, const BridgeProcessingConfig& config);
void bridge_set_realtime_roi(const AppBackendShim& shim, const BridgeRoi& roi);
void bridge_clear_realtime_roi(const AppBackendShim& shim);
bool bridge_set_realtime_background(const AppBackendShim& shim);
BridgeMonitoringFrames bridge_get_monitoring_frames(const AppBackendShim& shim);
void bridge_clear_monitoring_frames(const AppBackendShim& shim);

rust::String bridge_start_experiment(const AppBackendShim& shim, rust::Str hdf5_path);
rust::String bridge_stop_experiment(const AppBackendShim& shim);
rust::String bridge_load_hdf5_file(const AppBackendShim& shim, rust::Str path);
rust::Vec<BridgeProcessedFrame> bridge_get_hdf5_valid_frames(const AppBackendShim& shim);
rust::Vec<BridgeProcessedFrame> bridge_get_hdf5_invalid_frames(const AppBackendShim& shim);
rust::String bridge_export_metrics_csv(const AppBackendShim& shim, rust::Str hdf5_path, rust::Str output_path);
bool bridge_start_frame_recording(const AppBackendShim& shim, rust::Str hdf5_path);
void bridge_stop_frame_recording(const AppBackendShim& shim);

bool bridge_connect_autofocus(const AppBackendShim& shim,
                              std::int32_t com_port,
                              std::int32_t baud_rate,
                              std::uint8_t device_address);
void bridge_disconnect_autofocus(const AppBackendShim& shim);
void bridge_set_autofocus_enabled(const AppBackendShim& shim, bool enabled);
void bridge_increase_voltage(const AppBackendShim& shim);
void bridge_decrease_voltage(const AppBackendShim& shim);
BridgeAutofocusConfig bridge_get_autofocus_config(const AppBackendShim& shim);
void bridge_set_autofocus_config(const AppBackendShim& shim, const BridgeAutofocusConfig& config);

bool bridge_connect_pump(const AppBackendShim& shim,
                         std::int32_t pump_id,
                         std::int32_t com_port,
                         std::int32_t baud_rate,
                         std::uint8_t modbus_address);
void bridge_disconnect_pump(const AppBackendShim& shim, std::int32_t pump_id);
bool bridge_set_pump_flow_rate(const AppBackendShim& shim, std::int32_t pump_id, double rate, std::uint16_t unit);
bool bridge_set_pump_direction(const AppBackendShim& shim, std::int32_t pump_id, rust::Str direction);
bool bridge_start_pump(const AppBackendShim& shim, std::int32_t pump_id);
bool bridge_stop_pump(const AppBackendShim& shim, std::int32_t pump_id);
bool bridge_purge_pump(const AppBackendShim& shim, std::int32_t pump_id, rust::Str direction);
BridgePumpStatus bridge_get_pump_status(const AppBackendShim& shim, std::int32_t pump_id);
BridgePumpConfig bridge_get_pump_config(const AppBackendShim& shim, std::int32_t pump_id);

void bridge_fire_sort_trigger(const AppBackendShim& shim);
void bridge_set_trigger_duration(const AppBackendShim& shim, std::int32_t duration_us);

rust::String bridge_get_app_config(const AppBackendShim& shim);
void bridge_set_app_config(const AppBackendShim& shim, rust::Str json);
rust::String bridge_apply_camera_script(const AppBackendShim& shim, rust::Str path);
void bridge_set_pixel_to_micron_factor(const AppBackendShim& shim, double factor);
rust::String bridge_save_buffer_to_disk(const AppBackendShim& shim,
                                        rust::Str output_dir,
                                        std::uint64_t start_index,
                                        std::uint64_t end_index,
                                        bool use_range);

void bridge_install_emitters(AppBackendShim& shim);
