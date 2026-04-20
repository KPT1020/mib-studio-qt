#[cxx::bridge]
pub mod ffi_inner {
    struct BridgeCamera {
        interface_index: i32,
        device_index: i32,
        interface_id: String,
        device_id: String,
        model_name: String,
        firmware_version: String,
        label: String,
    }

    struct BridgeFramegrabber {
        interface_index: i32,
        device_index: i32,
        stream_index: i32,
        interface_id: String,
        device_id: String,
        stream_id: String,
        model_name: String,
        label: String,
    }

    struct BridgePlaybackRange {
        earliest: u64,
        latest: u64,
        count: u64,
    }

    struct BridgeFrameMeta {
        index: u64,
        width: u64,
        height: u64,
        timestamp_ns: u64,
    }

    unsafe extern "C++" {
        include!("bridge/bridge.h");

        type AppBackendShim;

        fn create_shim(data_dir: &str) -> UniquePtr<AppBackendShim>;
        fn backend_version(self: &AppBackendShim) -> String;

        fn bridge_discover_cameras(shim: &AppBackendShim) -> Vec<BridgeCamera>;
        fn bridge_discover_framegrabbers(shim: &AppBackendShim) -> Vec<BridgeFramegrabber>;
        fn bridge_set_hardware_camera(
            shim: &AppBackendShim,
            interface_index: i32,
            device_index: i32,
            label: &str,
        );
        fn bridge_configure_mock(
            shim: &AppBackendShim,
            dir: &str,
            interval_ms: u32,
            loop_files: bool,
        ) -> bool;
        fn bridge_start_capture(shim: &AppBackendShim) -> bool;
        fn bridge_stop_capture(shim: &AppBackendShim);
        fn bridge_is_capture_running(shim: &AppBackendShim) -> bool;
        fn bridge_fetch_latest_frame_png(shim: &AppBackendShim) -> Vec<u8>;
        fn bridge_fetch_latest_frame_meta(shim: &AppBackendShim) -> BridgeFrameMeta;
        fn bridge_get_playback_range(shim: &AppBackendShim) -> BridgePlaybackRange;
        fn bridge_install_emitters(shim: Pin<&mut AppBackendShim>);
    }

    extern "Rust" {
        fn mib_emit_frame(
            index: u64,
            width: u64,
            height: u64,
            timestamp_ns: u64,
            png_bytes: Vec<u8>,
        );
        fn mib_emit_stats(
            capture_frame_rate: f64,
            capture_data_rate_mbps: f64,
            algo_fps: f64,
            valid_fps: f64,
            invalid_fps: f64,
            algo_avg_us: f64,
            total_valid_flushed: u64,
        );
    }
}

pub use ffi_inner::*;

// Implemented alongside #[cxx::bridge] so `extern "Rust"` can resolve `super::mib_emit_*`.
fn mib_emit_frame(
    index: u64,
    width: u64,
    height: u64,
    timestamp_ns: u64,
    png_bytes: Vec<u8>,
) {
    crate::bridge::emit_frame_from_cpp(index, width, height, timestamp_ns, png_bytes);
}

fn mib_emit_stats(
    capture_frame_rate: f64,
    capture_data_rate_mbps: f64,
    algo_fps: f64,
    valid_fps: f64,
    invalid_fps: f64,
    algo_avg_us: f64,
    total_valid_flushed: u64,
) {
    crate::bridge::emit_stats_from_cpp(
        capture_frame_rate,
        capture_data_rate_mbps,
        algo_fps,
        valid_fps,
        invalid_fps,
        algo_avg_us,
        total_valid_flushed,
    );
}
