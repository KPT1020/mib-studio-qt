#pragma once

// C++ FFI Bridge Layer for Tauri/Rust Integration
// This header declares C-compatible functions that wrap the AppBackend
// service methods. These are bound into Rust via the cxx crate.
//
// Design principles:
// - Simple types at the boundary (int, double, bool, const char*)
// - Complex return types serialized as JSON strings
// - Frame pixel data returned as raw byte buffers with metadata
// - Thread-safe: all functions synchronize internally

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Lifecycle ----
int bridge_initialize(const char* data_dir);
void bridge_shutdown();

// ---- Camera Control ----
// Returns JSON array of DiscoveredCamera objects
const char* bridge_discover_cameras();
// Returns JSON array of DiscoveredFramegrabber objects
const char* bridge_discover_framegrabbers();
int bridge_connect_camera(int interface_index, int device_index, const char* label);
int bridge_configure_mock(const char* directory, int interval_ms, int loop);

// ---- Capture ----
int bridge_capture_start();
void bridge_capture_stop();
int bridge_capture_is_running();

// ---- Playback ----
// Returns JSON FrameData or NULL if no frame available
const char* bridge_fetch_latest_frame();
const char* bridge_fetch_frame_by_index(uint64_t index);
// Returns JSON PlaybackRange {earliest, latest, count}
const char* bridge_get_playback_range();

// ---- Processing ----
// Returns JSON ProcessingConfig
const char* bridge_get_processing_config();
// Takes JSON ProcessingConfig
int bridge_set_processing_config(const char* config_json);
int bridge_set_realtime_roi(int x, int y, int w, int h);
int bridge_clear_realtime_roi();
int bridge_set_realtime_background();
// Returns JSON {valid: ProcessedFrame[], invalid: ProcessedFrame[]}
const char* bridge_get_monitoring_frames();
void bridge_clear_monitoring_frames();

// ---- Processing Stats ----
double bridge_get_algo_fps();
double bridge_get_valid_fps();
double bridge_get_invalid_fps();
double bridge_get_algo_avg_us();
uint64_t bridge_get_total_valid_flushed();

// ---- HDF5 / Experiment ----
int bridge_start_experiment(const char* hdf5_path);
int bridge_stop_experiment();
int bridge_load_hdf5_file(const char* path);
// Returns JSON ProcessedFrame[]
const char* bridge_get_hdf5_valid_frames();
const char* bridge_get_hdf5_invalid_frames();
int bridge_export_metrics_csv(const char* hdf5_path, const char* output_path);

// ---- Recording ----
int bridge_start_frame_recording(const char* hdf5_path);
void bridge_stop_frame_recording();
int bridge_is_frame_recording();
uint64_t bridge_frame_recording_count();

// ---- Autofocus ----
int bridge_connect_autofocus(int com_port, int baud_rate, uint8_t device_address);
void bridge_disconnect_autofocus();
int bridge_autofocus_is_connected();
void bridge_set_autofocus_enabled(int enabled);
void bridge_increase_voltage();
void bridge_decrease_voltage();
double bridge_get_current_voltage();
// Returns JSON AutofocusConfig
const char* bridge_get_autofocus_config();
int bridge_set_autofocus_config(const char* config_json);

// ---- Syringe Pump ----
// pump_id: 0 = Sample, 1 = Sheath
int bridge_connect_pump(int pump_id, int com_port, int baud_rate, uint8_t modbus_address);
void bridge_disconnect_pump(int pump_id);
int bridge_pump_is_connected(int pump_id);
int bridge_set_pump_flow_rate(int pump_id, double rate, uint16_t unit);
int bridge_set_pump_direction(int pump_id, int direction);
int bridge_start_pump(int pump_id);
int bridge_stop_pump(int pump_id);
int bridge_purge_pump(int pump_id, int direction);
// Returns JSON PumpStatus
const char* bridge_get_pump_status(int pump_id);

// ---- Trigger ----
void bridge_fire_sort_trigger();
void bridge_set_trigger_duration_us(int duration_us);

// ---- Config ----
const char* bridge_get_app_config();
int bridge_set_app_config(const char* json);
// Returns error string or NULL on success
const char* bridge_apply_camera_script(const char* path);
void bridge_set_pixel_to_micron_factor(double factor);

// ---- Buffer Save ----
int bridge_save_buffer_to_disk(const char* output_dir, uint64_t start_index, uint64_t end_index);

// ---- Callbacks ----
// Register callback functions that Rust will provide
typedef void (*frame_callback_t)(uint64_t index, const uint8_t* jpeg_data, size_t jpeg_size,
                                  uint64_t width, uint64_t height, uint64_t timestamp_ns);
typedef void (*stats_callback_t)(double capture_fps, double data_rate,
                                  double algo_fps, double valid_fps, double invalid_fps,
                                  double algo_avg_us, uint64_t total_valid);
typedef void (*background_callback_t)(const uint8_t* jpeg_data, size_t jpeg_size, uint64_t frame_index);

void bridge_set_frame_callback(frame_callback_t callback);
void bridge_set_stats_callback(stats_callback_t callback);
void bridge_set_background_callback(background_callback_t callback);

// ---- Memory Management ----
// Free strings returned by bridge functions
void bridge_free_string(const char* str);

#ifdef __cplusplus
}
#endif
