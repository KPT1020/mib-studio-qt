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

    struct BridgeRoi {
        x: i32,
        y: i32,
        w: i32,
        h: i32,
    }

    struct BridgeProcessingConfig {
        gaussian_blur_size: i32,
        bg_subtract_threshold: i32,
        morph_kernel_size: i32,
        morph_iterations: i32,
        area_threshold_min: i32,
        area_threshold_max: i32,
        deformability_threshold_min: f64,
        deformability_threshold_max: f64,
        enable_border_check: bool,
        enable_area_range_check: bool,
        enable_deformability_range_check: bool,
        area_ratio_threshold_max: f64,
        enable_area_ratio_check: bool,
        ring_ratio_min: f64,
        ring_ratio_max: f64,
        enable_ring_ratio_check: bool,
        require_single_inner_contour: bool,
        empty_frame_pixel_threshold: i32,
        auto_background_enabled: bool,
        auto_background_empty_frames: i32,
        auto_background_cooldown_frames: i32,
        enable_target_group: bool,
        target_group_area_min: i32,
        target_group_area_max: i32,
        target_group_deformability_min: f64,
        target_group_deformability_max: f64,
        enable_target_group_emodulus: bool,
        target_group_emodulus_min: f64,
        target_group_emodulus_max: f64,
        multi_image_enabled: bool,
        multi_image_count: i32,
    }

    struct BridgeBrightnessQuantiles {
        q1: f64,
        q2: f64,
        q3: f64,
        q4: f64,
    }

    struct BridgeFilterResult {
        is_valid: bool,
        touches_border: bool,
        has_single_inner_contour: bool,
        in_range: bool,
        inner_contour_count: i32,
        deformability: f64,
        area: f64,
        area_ratio: f64,
        ring_ratio: f64,
        youngs_modulus: f64,
        brightness: BridgeBrightnessQuantiles,
        is_target_group: bool,
    }

    struct BridgeProcessedFrame {
        index: u64,
        timestamp_ns: u64,
        image_base64: String,
        image_width: u32,
        image_height: u32,
        validation: BridgeFilterResult,
    }

    struct BridgeMonitoringFrames {
        valid: Vec<BridgeProcessedFrame>,
        invalid: Vec<BridgeProcessedFrame>,
    }

    struct BridgeAutofocusConfig {
        focus_setpoint: f64,
        focus_range: f64,
        voltage_step: f64,
        fine_voltage_step: f64,
        max_voltage: f64,
        min_voltage: f64,
        initial_voltage: f64,
        manual_voltage_step: f64,
        ring_ratio_stale_ms: i32,
        require_new_sample_per_step: bool,
        min_samples_per_step: i32,
        safe_shutdown_voltage: f64,
        focus_direction: bool,
    }

    struct BridgePumpStatus {
        connected: bool,
        run_status: String,
        current_flow_rate: f64,
        accumulated_volume: f64,
        min_flow_rate: f64,
        max_flow_rate: f64,
        stalled: bool,
    }

    struct BridgePumpConfig {
        com_port: i32,
        baud_rate: i32,
        modbus_address: u8,
        flow_rate: f64,
        flow_rate_unit: u16,
        direction: String,
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
        ) -> String;
        fn bridge_start_capture(shim: &AppBackendShim) -> bool;
        fn bridge_stop_capture(shim: &AppBackendShim);
        fn bridge_is_capture_running(shim: &AppBackendShim) -> bool;
        fn bridge_fetch_latest_frame_png(shim: &AppBackendShim) -> Vec<u8>;
        fn bridge_fetch_latest_frame_meta(shim: &AppBackendShim) -> BridgeFrameMeta;
        fn bridge_fetch_frame_by_index_png(shim: &AppBackendShim, index: u64) -> Vec<u8>;
        fn bridge_fetch_frame_by_index_meta(shim: &AppBackendShim, index: u64) -> BridgeFrameMeta;
        fn bridge_get_playback_range(shim: &AppBackendShim) -> BridgePlaybackRange;

        fn bridge_get_processing_config(shim: &AppBackendShim) -> BridgeProcessingConfig;
        fn bridge_set_processing_config(shim: &AppBackendShim, config: &BridgeProcessingConfig);
        fn bridge_set_realtime_roi(shim: &AppBackendShim, roi: &BridgeRoi);
        fn bridge_clear_realtime_roi(shim: &AppBackendShim);
        fn bridge_set_realtime_background(shim: &AppBackendShim) -> bool;
        fn bridge_get_monitoring_frames(shim: &AppBackendShim) -> BridgeMonitoringFrames;
        fn bridge_clear_monitoring_frames(shim: &AppBackendShim);

        fn bridge_start_experiment(shim: &AppBackendShim, hdf5_path: &str) -> String;
        fn bridge_stop_experiment(shim: &AppBackendShim) -> String;
        fn bridge_load_hdf5_file(shim: &AppBackendShim, path: &str) -> String;
        fn bridge_get_hdf5_valid_frames(shim: &AppBackendShim) -> Vec<BridgeProcessedFrame>;
        fn bridge_get_hdf5_invalid_frames(shim: &AppBackendShim) -> Vec<BridgeProcessedFrame>;
        fn bridge_export_metrics_csv(
            shim: &AppBackendShim,
            hdf5_path: &str,
            output_path: &str,
        ) -> String;
        fn bridge_start_frame_recording(shim: &AppBackendShim, hdf5_path: &str) -> bool;
        fn bridge_stop_frame_recording(shim: &AppBackendShim);

        fn bridge_connect_autofocus(
            shim: &AppBackendShim,
            com_port: i32,
            baud_rate: i32,
            device_address: u8,
        ) -> bool;
        fn bridge_disconnect_autofocus(shim: &AppBackendShim);
        fn bridge_set_autofocus_enabled(shim: &AppBackendShim, enabled: bool);
        fn bridge_increase_voltage(shim: &AppBackendShim);
        fn bridge_decrease_voltage(shim: &AppBackendShim);
        fn bridge_get_autofocus_config(shim: &AppBackendShim) -> BridgeAutofocusConfig;
        fn bridge_set_autofocus_config(shim: &AppBackendShim, config: &BridgeAutofocusConfig);

        fn bridge_connect_pump(
            shim: &AppBackendShim,
            pump_id: i32,
            com_port: i32,
            baud_rate: i32,
            modbus_address: u8,
        ) -> bool;
        fn bridge_disconnect_pump(shim: &AppBackendShim, pump_id: i32);
        fn bridge_set_pump_flow_rate(
            shim: &AppBackendShim,
            pump_id: i32,
            rate: f64,
            unit: u16,
        ) -> bool;
        fn bridge_set_pump_direction(shim: &AppBackendShim, pump_id: i32, direction: &str) -> bool;
        fn bridge_start_pump(shim: &AppBackendShim, pump_id: i32) -> bool;
        fn bridge_stop_pump(shim: &AppBackendShim, pump_id: i32) -> bool;
        fn bridge_purge_pump(shim: &AppBackendShim, pump_id: i32, direction: &str) -> bool;
        fn bridge_get_pump_status(shim: &AppBackendShim, pump_id: i32) -> BridgePumpStatus;
        fn bridge_get_pump_config(shim: &AppBackendShim, pump_id: i32) -> BridgePumpConfig;

        fn bridge_fire_sort_trigger(shim: &AppBackendShim);
        fn bridge_set_trigger_duration(shim: &AppBackendShim, duration_us: i32);

        fn bridge_get_app_config(shim: &AppBackendShim) -> String;
        fn bridge_set_app_config(shim: &AppBackendShim, json: &str);
        fn bridge_apply_camera_script(shim: &AppBackendShim, path: &str) -> String;
        fn bridge_set_pixel_to_micron_factor(shim: &AppBackendShim, factor: f64);
        fn bridge_save_buffer_to_disk(
            shim: &AppBackendShim,
            output_dir: &str,
            start_index: u64,
            end_index: u64,
            use_range: bool,
        ) -> String;

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
        fn mib_emit_background(frame_index: u64, png_bytes: Vec<u8>);
    }
}

pub use ffi_inner::*;

// Implemented alongside #[cxx::bridge] so `extern "Rust"` can resolve `super::mib_emit_*`.
fn mib_emit_frame(index: u64, width: u64, height: u64, timestamp_ns: u64, png_bytes: Vec<u8>) {
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

fn mib_emit_background(frame_index: u64, png_bytes: Vec<u8>) {
    crate::bridge::emit_background_from_cpp(frame_index, png_bytes);
}
