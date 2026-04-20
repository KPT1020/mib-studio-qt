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

    /// Used by cxx free functions in bridge.cpp.
    backend::AppBackend& app_backend() const { return *backend_; }
    backend::AppBackend* app_backend_ptr() const { return backend_.get(); }

    void install_emitters_impl();

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
BridgePlaybackRange bridge_get_playback_range(const AppBackendShim& shim);
void bridge_install_emitters(AppBackendShim& shim);
