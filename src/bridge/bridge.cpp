// C++ FFI Bridge Implementation
// Wraps AppBackend service methods for Rust/Tauri integration via cxx
//
// NOTE: This is a stub implementation. Each function needs to be connected
// to the actual AppBackend instance once the cxx build integration is complete.

#include "bridge/bridge.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <mutex>

// TODO: Include when build integration is ready
// #include "backend/AppBackend.h"

// Global backend instance (initialized in bridge_initialize)
// static backend::AppBackend* g_backend = nullptr;
static std::mutex g_mutex;

// Helper: duplicate a std::string to a C string that Rust can free
static char* to_c_string(const std::string& s) {
    char* result = static_cast<char*>(std::malloc(s.size() + 1));
    if (result) {
        std::memcpy(result, s.c_str(), s.size() + 1);
    }
    return result;
}

// ---- Lifecycle ----

int bridge_initialize(const char* data_dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    // TODO: Create and initialize AppBackend
    // g_backend = new backend::AppBackend();
    // return g_backend->initialize(data_dir) ? 1 : 0;
    return 1;
}

void bridge_shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    // TODO: Clean up AppBackend
    // delete g_backend;
    // g_backend = nullptr;
}

// ---- Camera Control ----

const char* bridge_discover_cameras() {
    // TODO: Call g_backend->cameraControl().discoverCameras()
    // Serialize to JSON array
    return to_c_string("[]");
}

const char* bridge_discover_framegrabbers() {
    // TODO: Call g_backend->cameraControl().discoverFramegrabbers()
    return to_c_string("[]");
}

int bridge_connect_camera(int interface_index, int device_index, const char* label) {
    // TODO: Call g_backend->setHardwareCameraSelection(...)
    return 1;
}

int bridge_configure_mock(const char* directory, int interval_ms, int loop) {
    // TODO: Call g_backend->configureMockCamera(...)
    return 1;
}

// ---- Capture ----

int bridge_capture_start() {
    // TODO: Call g_backend->capture().start()
    return 1;
}

void bridge_capture_stop() {
    // TODO: Call g_backend->capture().stop()
}

int bridge_capture_is_running() {
    // TODO: Call g_backend->capture().isRunning()
    return 0;
}

// ---- Playback ----

const char* bridge_fetch_latest_frame() {
    // TODO: Fetch latest frame, JPEG-encode, return as JSON
    return nullptr;
}

const char* bridge_fetch_frame_by_index(uint64_t index) {
    // TODO: Fetch frame by index, JPEG-encode, return as JSON
    return nullptr;
}

const char* bridge_get_playback_range() {
    // TODO: Call g_backend->playback().queryRange()
    return to_c_string("{\"earliest\":0,\"latest\":0,\"count\":0}");
}

// ---- Processing ----

const char* bridge_get_processing_config() {
    // TODO: Serialize g_backend->processing().getProcessingConfig() to JSON
    return to_c_string("{}");
}

int bridge_set_processing_config(const char* config_json) {
    // TODO: Deserialize JSON, call g_backend->processing().setProcessingConfig(...)
    return 1;
}

int bridge_set_realtime_roi(int x, int y, int w, int h) {
    // TODO: Call g_backend->processing().setRealtimeRoi({x, y, w, h})
    return 1;
}

int bridge_clear_realtime_roi() {
    // TODO: Call g_backend->processing().setRealtimeRoi({0, 0, 0, 0})
    return 1;
}

int bridge_set_realtime_background() {
    // TODO: Capture current frame as background
    return 1;
}

const char* bridge_get_monitoring_frames() {
    // TODO: Serialize monitoring frames to JSON
    return to_c_string("{\"valid\":[],\"invalid\":[]}");
}

void bridge_clear_monitoring_frames() {
    // TODO: Call g_backend->processing().clearMonitoringFrames()
}

// ---- Processing Stats ----

double bridge_get_algo_fps() { return 0.0; }
double bridge_get_valid_fps() { return 0.0; }
double bridge_get_invalid_fps() { return 0.0; }
double bridge_get_algo_avg_us() { return 0.0; }
uint64_t bridge_get_total_valid_flushed() { return 0; }

// ---- HDF5 ----

int bridge_start_experiment(const char* hdf5_path) {
    // TODO: Call g_backend->hdf5().openFile(path)
    return 1;
}

int bridge_stop_experiment() {
    // TODO: Call g_backend->hdf5().closeFile()
    return 1;
}

int bridge_load_hdf5_file(const char* path) {
    // TODO: Call g_backend->hdf5().loadFile(path)
    return 1;
}

const char* bridge_get_hdf5_valid_frames() {
    return to_c_string("[]");
}

const char* bridge_get_hdf5_invalid_frames() {
    return to_c_string("[]");
}

int bridge_export_metrics_csv(const char* /*hdf5_path*/, const char* /*output_path*/) {
    return 1;
}

// ---- Recording ----

int bridge_start_frame_recording(const char* hdf5_path) { return 1; }
void bridge_stop_frame_recording() {}
int bridge_is_frame_recording() { return 0; }
uint64_t bridge_frame_recording_count() { return 0; }

// ---- Autofocus ----

int bridge_connect_autofocus(int /*com_port*/, int /*baud_rate*/, uint8_t /*device_address*/) {
    return 1;
}

void bridge_disconnect_autofocus() {}
int bridge_autofocus_is_connected() { return 0; }
void bridge_set_autofocus_enabled(int /*enabled*/) {}
void bridge_increase_voltage() {}
void bridge_decrease_voltage() {}
double bridge_get_current_voltage() { return 0.0; }
const char* bridge_get_autofocus_config() { return to_c_string("{}"); }
int bridge_set_autofocus_config(const char* /*config_json*/) { return 1; }

// ---- Syringe Pump ----

int bridge_connect_pump(int /*pump_id*/, int /*com_port*/, int /*baud_rate*/, uint8_t /*modbus_address*/) { return 1; }
void bridge_disconnect_pump(int /*pump_id*/) {}
int bridge_pump_is_connected(int /*pump_id*/) { return 0; }
int bridge_set_pump_flow_rate(int /*pump_id*/, double /*rate*/, uint16_t /*unit*/) { return 1; }
int bridge_set_pump_direction(int /*pump_id*/, int /*direction*/) { return 1; }
int bridge_start_pump(int /*pump_id*/) { return 1; }
int bridge_stop_pump(int /*pump_id*/) { return 1; }
int bridge_purge_pump(int /*pump_id*/, int /*direction*/) { return 1; }
const char* bridge_get_pump_status(int /*pump_id*/) {
    return to_c_string("{\"connected\":false,\"run_status\":\"stop\",\"current_flow_rate\":0,\"accumulated_volume\":0,\"min_flow_rate\":0,\"max_flow_rate\":0,\"stalled\":false}");
}

// ---- Trigger ----

void bridge_fire_sort_trigger() {}
void bridge_set_trigger_duration_us(int /*duration_us*/) {}

// ---- Config ----

const char* bridge_get_app_config() { return to_c_string("{}"); }
int bridge_set_app_config(const char* /*json*/) { return 1; }
const char* bridge_apply_camera_script(const char* /*path*/) { return nullptr; }
void bridge_set_pixel_to_micron_factor(double /*factor*/) {}

// ---- Buffer Save ----

int bridge_save_buffer_to_disk(const char* /*output_dir*/, uint64_t /*start_index*/, uint64_t /*end_index*/) {
    return 1;
}

// ---- Callbacks ----

static frame_callback_t g_frame_callback = nullptr;
static stats_callback_t g_stats_callback = nullptr;
static background_callback_t g_background_callback = nullptr;

void bridge_set_frame_callback(frame_callback_t callback) {
    g_frame_callback = callback;
}

void bridge_set_stats_callback(stats_callback_t callback) {
    g_stats_callback = callback;
}

void bridge_set_background_callback(background_callback_t callback) {
    g_background_callback = callback;
}

// ---- Memory Management ----

void bridge_free_string(const char* str) {
    std::free(const_cast<char*>(str));
}
