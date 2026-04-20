pub mod ffi;

use std::sync::OnceLock;

use anyhow::{anyhow, Result};
use base64::Engine;
use parking_lot::Mutex;
use tauri::{AppHandle, Emitter};

use crate::events::{FrameNewPayload, StatsUpdatePayload, FRAME_NEW, STATS_UPDATE};

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
        Ok(ffi::bridge_discover_cameras(guard.as_ref().expect("shim null")))
    }

    pub fn discover_framegrabbers(&self) -> Result<Vec<ffi::BridgeFramegrabber>> {
        let guard = self.shim.lock();
        Ok(ffi::bridge_discover_framegrabbers(
            guard.as_ref().expect("shim null"),
        ))
    }

    pub fn set_hardware_camera(&self, interface_index: i32, device_index: i32, label: &str) -> Result<()> {
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
        ffi::bridge_configure_mock(
            guard.as_ref().expect("shim null"),
            dir,
            interval_ms,
            loop_files,
        );
        Ok(())
    }

    pub fn start_capture(&self) -> Result<()> {
        let guard = self.shim.lock();
        if !ffi::bridge_start_capture(guard.as_ref().expect("shim null")) {
            return Err(anyhow!("capture start failed (camera not configured or start error)"));
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
