pub mod ffi;

use std::sync::OnceLock;

use anyhow::{anyhow, Result};
use base64::Engine;
use parking_lot::Mutex;
use tauri::{AppHandle, Emitter};

use crate::events::{
    BackgroundCapturedPayload, FrameNewPayload, StatsUpdatePayload, BACKGROUND_CAPTURED, FRAME_NEW,
    STATS_UPDATE,
};

static APP_HANDLE: OnceLock<AppHandle> = OnceLock::new();

pub struct Backend {
    shim: Mutex<cxx::UniquePtr<ffi::AppBackendShim>>,
}

// AppBackend is thread-safe internally (services synchronize themselves).
// The cxx UniquePtr holds an opaque C++ type; we take the mutex on every call.
unsafe impl Send for Backend {}
unsafe impl Sync for Backend {}

impl Backend {
    pub fn new(data_dir: &str) -> Result<Self> {
        let shim = ffi::create_shim(data_dir);
        if shim.is_null() {
            return Err(anyhow!("AppBackend::initialize failed for {data_dir}"));
        }
        Ok(Self {
            shim: Mutex::new(shim),
        })
    }

    pub fn version(&self) -> String {
        let guard = self.shim.lock();
        guard.as_ref().expect("shim null").backend_version()
    }

    /// Call after `tauri::Manager::manage(AppState)` so we can emit events.
    pub fn register_emitters(&self, app: &AppHandle) -> Result<()> {
        APP_HANDLE
            .set(app.clone())
            .map_err(|_| anyhow!("APP_HANDLE already set"))?;

        let mut guard = self.shim.lock();
        ffi::bridge_install_emitters(guard.pin_mut());
        Ok(())
    }

    pub fn discover_cameras(&self) -> Result<Vec<ffi::BridgeCamera>> {
        let guard = self.shim.lock();
        Ok(ffi::bridge_discover_cameras(
            guard.as_ref().expect("shim null"),
        ))
    }

    pub fn discover_framegrabbers(&self) -> Result<Vec<ffi::BridgeFramegrabber>> {
        let guard = self.shim.lock();
        Ok(ffi::bridge_discover_framegrabbers(
            guard.as_ref().expect("shim null"),
        ))
    }

    pub fn set_hardware_camera(
        &self,
        interface_index: i32,
        device_index: i32,
        label: &str,
    ) -> Result<()> {
        let guard = self.shim.lock();
        ffi::bridge_set_hardware_camera(
            guard.as_ref().expect("shim null"),
            interface_index,
            device_index,
            label,
        );
        Ok(())
    }

    pub fn configure_mock(&self, dir: &str, interval_ms: u32, loop_files: bool) -> Result<()> {
        let guard = self.shim.lock();
        let err = ffi::bridge_configure_mock(
            guard.as_ref().expect("shim null"),
            dir,
            interval_ms,
            loop_files,
        );
        if !err.is_empty() {
            return Err(anyhow!("{err}"));
        }
        Ok(())
    }

    pub fn start_capture(&self) -> Result<()> {
        let guard = self.shim.lock();
        if !ffi::bridge_start_capture(guard.as_ref().expect("shim null")) {
            return Err(anyhow!(
                "capture start failed (camera not configured or start error)"
            ));
        }
        Ok(())
    }

    pub fn stop_capture(&self) {
        let guard = self.shim.lock();
        ffi::bridge_stop_capture(guard.as_ref().expect("shim null"));
    }

    pub fn is_capture_running(&self) -> bool {
        let guard = self.shim.lock();
        ffi::bridge_is_capture_running(guard.as_ref().expect("shim null"))
    }

    pub fn fetch_latest_frame_png(&self) -> Vec<u8> {
        let guard = self.shim.lock();
        ffi::bridge_fetch_latest_frame_png(guard.as_ref().expect("shim null"))
    }

    pub fn fetch_latest_frame_meta(&self) -> ffi::BridgeFrameMeta {
        let guard = self.shim.lock();
        ffi::bridge_fetch_latest_frame_meta(guard.as_ref().expect("shim null"))
    }

    pub fn get_playback_range(&self) -> ffi::BridgePlaybackRange {
        let guard = self.shim.lock();
        ffi::bridge_get_playback_range(guard.as_ref().expect("shim null"))
    }

    pub fn fetch_frame_by_index_png(&self, index: u64) -> Vec<u8> {
        let guard = self.shim.lock();
        ffi::bridge_fetch_frame_by_index_png(guard.as_ref().expect("shim null"), index)
    }

    pub fn fetch_frame_by_index_meta(&self, index: u64) -> ffi::BridgeFrameMeta {
        let guard = self.shim.lock();
        ffi::bridge_fetch_frame_by_index_meta(guard.as_ref().expect("shim null"), index)
    }

    pub fn get_processing_config(&self) -> ffi::BridgeProcessingConfig {
        let guard = self.shim.lock();
        ffi::bridge_get_processing_config(guard.as_ref().expect("shim null"))
    }

    pub fn set_processing_config(&self, config: &ffi::BridgeProcessingConfig) {
        let guard = self.shim.lock();
        ffi::bridge_set_processing_config(guard.as_ref().expect("shim null"), config);
    }

    pub fn set_realtime_roi(&self, roi: &ffi::BridgeRoi) {
        let guard = self.shim.lock();
        ffi::bridge_set_realtime_roi(guard.as_ref().expect("shim null"), roi);
    }

    pub fn clear_realtime_roi(&self) {
        let guard = self.shim.lock();
        ffi::bridge_clear_realtime_roi(guard.as_ref().expect("shim null"));
    }

    pub fn set_realtime_background(&self) -> bool {
        let guard = self.shim.lock();
        ffi::bridge_set_realtime_background(guard.as_ref().expect("shim null"))
    }

    pub fn get_monitoring_frames(&self) -> ffi::BridgeMonitoringFrames {
        let guard = self.shim.lock();
        ffi::bridge_get_monitoring_frames(guard.as_ref().expect("shim null"))
    }

    pub fn clear_monitoring_frames(&self) {
        let guard = self.shim.lock();
        ffi::bridge_clear_monitoring_frames(guard.as_ref().expect("shim null"));
    }

    pub fn start_experiment(&self, hdf5_path: &str) -> Result<()> {
        let guard = self.shim.lock();
        let err = ffi::bridge_start_experiment(guard.as_ref().expect("shim null"), hdf5_path);
        if !err.is_empty() {
            return Err(anyhow!("{err}"));
        }
        Ok(())
    }

    pub fn stop_experiment(&self) -> Result<()> {
        let guard = self.shim.lock();
        let err = ffi::bridge_stop_experiment(guard.as_ref().expect("shim null"));
        if !err.is_empty() {
            return Err(anyhow!("{err}"));
        }
        Ok(())
    }

    pub fn load_hdf5_file(&self, path: &str) -> Result<()> {
        let guard = self.shim.lock();
        let err = ffi::bridge_load_hdf5_file(guard.as_ref().expect("shim null"), path);
        if !err.is_empty() {
            return Err(anyhow!("{err}"));
        }
        Ok(())
    }

    pub fn get_hdf5_valid_frames(&self) -> Vec<ffi::BridgeProcessedFrame> {
        let guard = self.shim.lock();
        ffi::bridge_get_hdf5_valid_frames(guard.as_ref().expect("shim null"))
    }

    pub fn get_hdf5_invalid_frames(&self) -> Vec<ffi::BridgeProcessedFrame> {
        let guard = self.shim.lock();
        ffi::bridge_get_hdf5_invalid_frames(guard.as_ref().expect("shim null"))
    }

    pub fn export_metrics_csv(&self, hdf5_path: &str, output_path: &str) -> Result<()> {
        let guard = self.shim.lock();
        let err = ffi::bridge_export_metrics_csv(
            guard.as_ref().expect("shim null"),
            hdf5_path,
            output_path,
        );
        if !err.is_empty() {
            return Err(anyhow!("{err}"));
        }
        Ok(())
    }

    pub fn start_frame_recording(&self, hdf5_path: &str) -> Result<()> {
        let guard = self.shim.lock();
        if !ffi::bridge_start_frame_recording(guard.as_ref().expect("shim null"), hdf5_path) {
            return Err(anyhow!("Failed to start frame recording"));
        }
        Ok(())
    }

    pub fn stop_frame_recording(&self) {
        let guard = self.shim.lock();
        ffi::bridge_stop_frame_recording(guard.as_ref().expect("shim null"));
    }

    pub fn connect_autofocus(
        &self,
        com_port: i32,
        baud_rate: i32,
        device_address: u8,
    ) -> Result<()> {
        let guard = self.shim.lock();
        if !ffi::bridge_connect_autofocus(
            guard.as_ref().expect("shim null"),
            com_port,
            baud_rate,
            device_address,
        ) {
            return Err(anyhow!("Failed to connect autofocus device"));
        }
        Ok(())
    }

    pub fn disconnect_autofocus(&self) {
        let guard = self.shim.lock();
        ffi::bridge_disconnect_autofocus(guard.as_ref().expect("shim null"));
    }

    pub fn set_autofocus_enabled(&self, enabled: bool) {
        let guard = self.shim.lock();
        ffi::bridge_set_autofocus_enabled(guard.as_ref().expect("shim null"), enabled);
    }

    pub fn increase_voltage(&self) {
        let guard = self.shim.lock();
        ffi::bridge_increase_voltage(guard.as_ref().expect("shim null"));
    }

    pub fn decrease_voltage(&self) {
        let guard = self.shim.lock();
        ffi::bridge_decrease_voltage(guard.as_ref().expect("shim null"));
    }

    pub fn get_autofocus_config(&self) -> ffi::BridgeAutofocusConfig {
        let guard = self.shim.lock();
        ffi::bridge_get_autofocus_config(guard.as_ref().expect("shim null"))
    }

    pub fn set_autofocus_config(&self, config: &ffi::BridgeAutofocusConfig) {
        let guard = self.shim.lock();
        ffi::bridge_set_autofocus_config(guard.as_ref().expect("shim null"), config);
    }

    pub fn connect_pump(
        &self,
        pump_id: i32,
        com_port: i32,
        baud_rate: i32,
        modbus_address: u8,
    ) -> Result<()> {
        let guard = self.shim.lock();
        if !ffi::bridge_connect_pump(
            guard.as_ref().expect("shim null"),
            pump_id,
            com_port,
            baud_rate,
            modbus_address,
        ) {
            return Err(anyhow!("Failed to connect pump"));
        }
        Ok(())
    }

    pub fn disconnect_pump(&self, pump_id: i32) {
        let guard = self.shim.lock();
        ffi::bridge_disconnect_pump(guard.as_ref().expect("shim null"), pump_id);
    }

    pub fn set_pump_flow_rate(&self, pump_id: i32, rate: f64, unit: u16) -> Result<()> {
        let guard = self.shim.lock();
        if !ffi::bridge_set_pump_flow_rate(guard.as_ref().expect("shim null"), pump_id, rate, unit)
        {
            return Err(anyhow!("Failed to set pump flow rate"));
        }
        Ok(())
    }

    pub fn set_pump_direction(&self, pump_id: i32, direction: &str) -> Result<()> {
        let guard = self.shim.lock();
        if !ffi::bridge_set_pump_direction(guard.as_ref().expect("shim null"), pump_id, direction) {
            return Err(anyhow!("Failed to set pump direction"));
        }
        Ok(())
    }

    pub fn start_pump(&self, pump_id: i32) -> Result<()> {
        let guard = self.shim.lock();
        if !ffi::bridge_start_pump(guard.as_ref().expect("shim null"), pump_id) {
            return Err(anyhow!("Failed to start pump"));
        }
        Ok(())
    }

    pub fn stop_pump(&self, pump_id: i32) -> Result<()> {
        let guard = self.shim.lock();
        if !ffi::bridge_stop_pump(guard.as_ref().expect("shim null"), pump_id) {
            return Err(anyhow!("Failed to stop pump"));
        }
        Ok(())
    }

    pub fn purge_pump(&self, pump_id: i32, direction: &str) -> Result<()> {
        let guard = self.shim.lock();
        if !ffi::bridge_purge_pump(guard.as_ref().expect("shim null"), pump_id, direction) {
            return Err(anyhow!("Failed to purge pump"));
        }
        Ok(())
    }

    pub fn get_pump_status(&self, pump_id: i32) -> ffi::BridgePumpStatus {
        let guard = self.shim.lock();
        ffi::bridge_get_pump_status(guard.as_ref().expect("shim null"), pump_id)
    }

    pub fn get_pump_config(&self, pump_id: i32) -> ffi::BridgePumpConfig {
        let guard = self.shim.lock();
        ffi::bridge_get_pump_config(guard.as_ref().expect("shim null"), pump_id)
    }

    pub fn fire_sort_trigger(&self) {
        let guard = self.shim.lock();
        ffi::bridge_fire_sort_trigger(guard.as_ref().expect("shim null"));
    }

    pub fn set_trigger_duration(&self, duration_us: i32) {
        let guard = self.shim.lock();
        ffi::bridge_set_trigger_duration(guard.as_ref().expect("shim null"), duration_us);
    }

    pub fn get_app_config(&self) -> String {
        let guard = self.shim.lock();
        ffi::bridge_get_app_config(guard.as_ref().expect("shim null"))
    }

    pub fn set_app_config(&self, json: &str) {
        let guard = self.shim.lock();
        ffi::bridge_set_app_config(guard.as_ref().expect("shim null"), json);
    }

    pub fn apply_camera_script(&self, path: &str) -> String {
        let guard = self.shim.lock();
        ffi::bridge_apply_camera_script(guard.as_ref().expect("shim null"), path)
    }

    pub fn set_pixel_to_micron_factor(&self, factor: f64) {
        let guard = self.shim.lock();
        ffi::bridge_set_pixel_to_micron_factor(guard.as_ref().expect("shim null"), factor);
    }

    pub fn save_buffer_to_disk(
        &self,
        output_dir: &str,
        start_index: u64,
        end_index: u64,
        use_range: bool,
    ) -> Result<()> {
        let guard = self.shim.lock();
        let err = ffi::bridge_save_buffer_to_disk(
            guard.as_ref().expect("shim null"),
            output_dir,
            start_index,
            end_index,
            use_range,
        );
        if !err.is_empty() {
            return Err(anyhow!("{err}"));
        }
        Ok(())
    }
}

pub(crate) fn emit_frame_from_cpp(
    index: u64,
    width: u64,
    height: u64,
    timestamp_ns: u64,
    png_bytes: Vec<u8>,
) {
    let Some(app) = APP_HANDLE.get() else {
        return;
    };
    let b64 = base64::engine::general_purpose::STANDARD.encode(&png_bytes);
    let payload = FrameNewPayload {
        index,
        width,
        height,
        image_base64: b64,
        timestamp_ns,
    };
    let _ = app.emit(FRAME_NEW, payload);
}

pub(crate) fn emit_background_from_cpp(frame_index: u64, png_bytes: Vec<u8>) {
    let Some(app) = APP_HANDLE.get() else {
        return;
    };
    let b64 = base64::engine::general_purpose::STANDARD.encode(&png_bytes);
    let payload = BackgroundCapturedPayload {
        image_base64: b64,
        frame_index,
    };
    let _ = app.emit(BACKGROUND_CAPTURED, payload);
}

pub(crate) fn emit_stats_from_cpp(
    capture_frame_rate: f64,
    capture_data_rate_mbps: f64,
    algo_fps: f64,
    valid_fps: f64,
    invalid_fps: f64,
    algo_avg_us: f64,
    total_valid_flushed: u64,
) {
    let Some(app) = APP_HANDLE.get() else {
        return;
    };
    let payload = StatsUpdatePayload {
        capture_frame_rate,
        capture_data_rate_mbps,
        algo_fps,
        valid_fps,
        invalid_fps,
        algo_avg_us,
        total_valid_flushed,
    };
    let _ = app.emit(STATS_UPDATE, payload);
}
