//! MIB Studio desktop shell (React + Tauri v2) — Phase 3 of epic #246.
//!
//! Wraps the Phase 2 Rust ↔ C++ bridge (`mib-bridge`, ADR 0003) as Tauri
//! commands so a React webview can drive the Qt-free C++ backend. The first
//! vertical slice is the **mock camera end to end**: configure a mock camera,
//! start capture, pull frames (raw Mono8 bytes — no per-frame base64), and
//! drain status/frame events.

use std::sync::Mutex;

use mib_bridge::ffi::{self, BridgeEventKind};
use serde::Serialize;
use tauri::ipc::Response;
use tauri::State;

struct AppState {
    bridge: Mutex<cxx::UniquePtr<ffi::BackendBridge>>,
    /// Pixel bytes of the last `fetch_frame` pull, so `frame_bytes` returns the
    /// exact frame `fetch_frame` described.
    last_frame: Mutex<Vec<u8>>,
}

/// Flattened command result handed to JS.
#[derive(Serialize, Clone)]
struct CmdResult {
    ok: bool,
    command: u32,
    message: String,
}

impl From<ffi::BridgeCommandResult> for CmdResult {
    fn from(r: ffi::BridgeCommandResult) -> Self {
        CmdResult { ok: r.ok, command: r.command, message: r.message }
    }
}

/// Frame metadata (pixel bytes are pulled separately via `frame_bytes`).
#[derive(Serialize, Clone, Default)]
struct FrameMeta {
    valid: bool,
    frame_index: u64,
    timestamp_ns: u64,
    width: u64,
    height: u64,
    pixel_format: u64,
    stride_bytes: u64,
    byte_len: u64,
}

/// Realtime processing stats snapshot for the webview.
#[derive(Serialize, Clone, Default)]
struct ProcessingStats {
    valid: bool,
    algo_fps1s: f64,
    valid_fps1s: f64,
    invalid_fps1s: f64,
    pixel_to_micron: f64,
}

/// Serde mirror of `BridgeEvent` for the webview. `kind` is a stable string;
/// the typed slots carry the per-kind fields (see the bridge's `shim.cpp`).
#[derive(Serialize, Clone)]
struct EventDto {
    kind: &'static str,
    u0: u64,
    u1: u64,
    u2: u64,
    u3: u64,
    u4: u64,
    u5: u64,
    f0: f64,
    f1: f64,
    f2: f64,
    b0: bool,
    b1: bool,
    text: String,
}

fn kind_name(k: BridgeEventKind) -> &'static str {
    match k {
        BridgeEventKind::FrameReady => "FrameReady",
        BridgeEventKind::CameraStatus => "CameraStatus",
        BridgeEventKind::RecordingStatus => "RecordingStatus",
        BridgeEventKind::ProcessingResult => "ProcessingResult",
        BridgeEventKind::PlaybackPosition => "PlaybackPosition",
        BridgeEventKind::BackendError => "BackendError",
        _ => "Unknown",
    }
}

#[tauri::command]
fn abi_version() -> u32 {
    ffi::bridge_abi_version()
}

#[tauri::command]
fn is_initialized(state: State<AppState>) -> Result<bool, String> {
    let guard = state.bridge.lock().map_err(|e| e.to_string())?;
    Ok(guard.is_initialized())
}

#[tauri::command]
fn init(state: State<AppState>, data_dir: String) -> Result<bool, String> {
    let mut guard = state.bridge.lock().map_err(|e| e.to_string())?;
    Ok(guard.pin_mut().initialize(&data_dir))
}

#[tauri::command]
fn configure_mock(
    state: State<AppState>,
    frame_dir: String,
    frame_interval_ms: i32,
    loop_files: bool,
) -> Result<CmdResult, String> {
    let mut guard = state.bridge.lock().map_err(|e| e.to_string())?;
    Ok(guard
        .pin_mut()
        .configure_mock_camera(&frame_dir, frame_interval_ms, loop_files)
        .into())
}

#[tauri::command]
fn start_capture(state: State<AppState>) -> Result<CmdResult, String> {
    let mut guard = state.bridge.lock().map_err(|e| e.to_string())?;
    Ok(guard.pin_mut().start_capture().into())
}

#[tauri::command]
fn stop_capture(state: State<AppState>) -> Result<CmdResult, String> {
    let mut guard = state.bridge.lock().map_err(|e| e.to_string())?;
    Ok(guard.pin_mut().stop_capture().into())
}

#[tauri::command]
fn seek_latest(state: State<AppState>) -> Result<CmdResult, String> {
    let mut guard = state.bridge.lock().map_err(|e| e.to_string())?;
    Ok(guard.pin_mut().playback_seek_latest().into())
}

#[tauri::command]
fn poll_events(state: State<AppState>) -> Result<Vec<EventDto>, String> {
    let mut guard = state.bridge.lock().map_err(|e| e.to_string())?;
    let events = guard.pin_mut().poll_events();
    Ok(events
        .into_iter()
        .map(|e| EventDto {
            kind: kind_name(e.kind),
            u0: e.u0,
            u1: e.u1,
            u2: e.u2,
            u3: e.u3,
            u4: e.u4,
            u5: e.u5,
            f0: e.f0,
            f1: e.f1,
            f2: e.f2,
            b0: e.b0,
            b1: e.b1,
            text: e.text,
        })
        .collect())
}

#[tauri::command]
fn start_recording(state: State<AppState>, file_path: String) -> Result<CmdResult, String> {
    let mut guard = state.bridge.lock().map_err(|e| e.to_string())?;
    Ok(guard.pin_mut().start_frame_recording(&file_path).into())
}

#[tauri::command]
fn stop_recording(state: State<AppState>) -> Result<CmdResult, String> {
    let mut guard = state.bridge.lock().map_err(|e| e.to_string())?;
    Ok(guard.pin_mut().stop_frame_recording().into())
}

#[tauri::command]
fn load_recording(state: State<AppState>, file_path: String) -> Result<CmdResult, String> {
    let mut guard = state.bridge.lock().map_err(|e| e.to_string())?;
    Ok(guard.pin_mut().load_recording(&file_path).into())
}

#[tauri::command]
fn seek_index(state: State<AppState>, frame_index: u64) -> Result<CmdResult, String> {
    let mut guard = state.bridge.lock().map_err(|e| e.to_string())?;
    Ok(guard.pin_mut().playback_seek_index(frame_index).into())
}

fn frame_to_meta(frame: &ffi::BridgeFrame) -> FrameMeta {
    FrameMeta {
        valid: frame.valid,
        frame_index: frame.frame_index,
        timestamp_ns: frame.timestamp_ns,
        width: frame.width,
        height: frame.height,
        pixel_format: frame.pixel_format,
        stride_bytes: frame.stride_bytes,
        byte_len: frame.data.len() as u64,
    }
}

/// Pull the latest frame, cache it, and return its metadata. Call `frame_bytes`
/// next to get the pixel bytes of this same frame.
#[tauri::command]
fn fetch_frame(state: State<AppState>) -> Result<FrameMeta, String> {
    let mut bridge = state.bridge.lock().map_err(|e| e.to_string())?;
    let frame = bridge.pin_mut().fetch_latest_frame();
    let meta = frame_to_meta(&frame);
    let mut last = state.last_frame.lock().map_err(|e| e.to_string())?;
    *last = frame.data;
    Ok(meta)
}

/// Pull a specific frame by index (review scrubbing), cache it, return metadata.
#[tauri::command]
fn fetch_frame_by_index(state: State<AppState>, frame_index: u64) -> Result<FrameMeta, String> {
    let mut bridge = state.bridge.lock().map_err(|e| e.to_string())?;
    let frame = bridge.pin_mut().fetch_frame_by_index(frame_index);
    let meta = frame_to_meta(&frame);
    let mut last = state.last_frame.lock().map_err(|e| e.to_string())?;
    *last = frame.data;
    Ok(meta)
}

/// Return the raw pixel bytes of the last `fetch_frame` result as a binary IPC
/// response — never base64-encoded (ADR 0003 hot-path rule).
#[tauri::command]
fn frame_bytes(state: State<AppState>) -> Result<Response, String> {
    let last = state.last_frame.lock().map_err(|e| e.to_string())?;
    Ok(Response::new(last.clone()))
}

#[tauri::command]
fn apply_processing(
    state: State<AppState>,
    realtime_enabled: bool,
    pixel_to_micron: f64,
) -> Result<CmdResult, String> {
    let mut guard = state.bridge.lock().map_err(|e| e.to_string())?;
    Ok(guard
        .pin_mut()
        .apply_processing(realtime_enabled, pixel_to_micron)
        .into())
}

#[tauri::command]
fn fetch_processing_stats(state: State<AppState>) -> Result<ProcessingStats, String> {
    let mut guard = state.bridge.lock().map_err(|e| e.to_string())?;
    let s = guard.pin_mut().fetch_processing_stats();
    Ok(ProcessingStats {
        valid: s.valid,
        algo_fps1s: s.algo_fps1s,
        valid_fps1s: s.valid_fps1s,
        invalid_fps1s: s.invalid_fps1s,
        pixel_to_micron: s.pixel_to_micron,
    })
}

#[cfg(test)]
mod tests {
    use super::kind_name;
    use mib_bridge::ffi::{self, BridgeEventKind};
    use std::time::{Duration, Instant};

    #[test]
    fn event_kind_names_are_stable() {
        assert_eq!(kind_name(BridgeEventKind::FrameReady), "FrameReady");
        assert_eq!(kind_name(BridgeEventKind::CameraStatus), "CameraStatus");
        assert_eq!(kind_name(BridgeEventKind::BackendError), "BackendError");
    }

    // Headless proof that the desktop crate links the bridge and the mock-camera
    // vertical slice works end to end (no Tauri runtime, no display).
    #[test]
    fn mock_camera_slice_round_trip() {
        let sample = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../data/mock_frames/frame_00000.tiff");
        assert!(sample.exists(), "sample frame missing: {}", sample.display());
        let dir = std::env::temp_dir().join(format!("mib_desktop_slice_{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        for i in 0..4 {
            std::fs::copy(&sample, dir.join(format!("frame_{i:04}.tiff"))).unwrap();
        }
        let data = std::env::temp_dir().join(format!("mib_desktop_data_{}", std::process::id()));

        let mut bridge = ffi::new_backend_bridge();
        assert!(bridge.pin_mut().initialize(&data.to_string_lossy()));
        assert!(bridge
            .pin_mut()
            .configure_mock_camera(&dir.to_string_lossy(), 5, true)
            .ok);
        assert!(bridge.pin_mut().start_capture().ok);

        let mut got = false;
        let deadline = Instant::now() + Duration::from_secs(5);
        while Instant::now() < deadline {
            let f = bridge.pin_mut().fetch_latest_frame();
            if f.valid {
                assert_eq!(f.width, 512);
                assert_eq!(f.height, 96);
                assert!(!f.data.is_empty());
                got = true;
                break;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        assert!(got, "no live frame within 5s");

        assert!(bridge.pin_mut().stop_capture().ok);
        bridge.pin_mut().shutdown();
        let _ = std::fs::remove_dir_all(&dir);
        let _ = std::fs::remove_dir_all(&data);
    }

    // Headless proof of the recording + review slice: record a clip, load it
    // back, and pull a frame by index.
    #[test]
    fn record_and_review_round_trip() {
        let sample = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../data/mock_frames/frame_00000.tiff");
        let dir = std::env::temp_dir().join(format!("mib_desktop_rev_{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        for i in 0..4 {
            std::fs::copy(&sample, dir.join(format!("frame_{i:04}.tiff"))).unwrap();
        }
        let data = std::env::temp_dir().join(format!("mib_desktop_rev_data_{}", std::process::id()));
        let rec = std::env::temp_dir().join(format!("mib_desktop_rev_{}.h5", std::process::id()));

        let mut bridge = ffi::new_backend_bridge();
        assert!(bridge.pin_mut().initialize(&data.to_string_lossy()));
        assert!(bridge
            .pin_mut()
            .configure_mock_camera(&dir.to_string_lossy(), 5, true)
            .ok);
        assert!(bridge.pin_mut().start_capture().ok);

        let deadline = Instant::now() + Duration::from_secs(5);
        while Instant::now() < deadline && !bridge.pin_mut().fetch_latest_frame().valid {
            std::thread::sleep(Duration::from_millis(10));
        }
        assert!(bridge.pin_mut().start_frame_recording(&rec.to_string_lossy()).ok);
        std::thread::sleep(Duration::from_millis(200));
        assert!(bridge.pin_mut().stop_frame_recording().ok);
        assert!(bridge.pin_mut().stop_capture().ok);

        assert!(bridge.pin_mut().load_recording(&rec.to_string_lossy()).ok);
        assert!(bridge.pin_mut().playback_seek_index(0).ok);
        let frame = bridge.pin_mut().fetch_frame_by_index(0);
        assert!(frame.valid && frame.width == 512 && frame.height == 96);

        bridge.pin_mut().shutdown();
        let _ = std::fs::remove_dir_all(&dir);
        let _ = std::fs::remove_dir_all(&data);
        let _ = std::fs::remove_file(&rec);
    }

    // Headless proof of the processing slice: apply settings, then pull stats.
    #[test]
    fn processing_settings_round_trip() {
        let sample = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../data/mock_frames/frame_00000.tiff");
        let dir = std::env::temp_dir().join(format!("mib_desktop_proc_{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        for i in 0..4 {
            std::fs::copy(&sample, dir.join(format!("frame_{i:04}.tiff"))).unwrap();
        }
        let data = std::env::temp_dir().join(format!("mib_desktop_proc_data_{}", std::process::id()));

        let mut bridge = ffi::new_backend_bridge();
        assert!(bridge.pin_mut().initialize(&data.to_string_lossy()));
        assert!(bridge
            .pin_mut()
            .configure_mock_camera(&dir.to_string_lossy(), 5, true)
            .ok);
        assert!(bridge.pin_mut().apply_processing(true, 3.0).ok);
        assert!(bridge.pin_mut().start_capture().ok);
        std::thread::sleep(Duration::from_millis(150));

        let stats = bridge.pin_mut().fetch_processing_stats();
        assert!(stats.valid);
        assert!((stats.pixel_to_micron - 3.0).abs() < 1e-9);

        assert!(bridge.pin_mut().stop_capture().ok);
        bridge.pin_mut().shutdown();
        let _ = std::fs::remove_dir_all(&dir);
        let _ = std::fs::remove_dir_all(&data);
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(AppState {
            bridge: Mutex::new(ffi::new_backend_bridge()),
            last_frame: Mutex::new(Vec::new()),
        })
        .invoke_handler(tauri::generate_handler![
            abi_version,
            is_initialized,
            init,
            configure_mock,
            start_capture,
            stop_capture,
            seek_latest,
            poll_events,
            fetch_frame,
            frame_bytes,
            start_recording,
            stop_recording,
            load_recording,
            seek_index,
            fetch_frame_by_index,
            apply_processing,
            fetch_processing_stats,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
