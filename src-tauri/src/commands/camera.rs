use serde::{Deserialize, Serialize};

use crate::bridge::ffi;
use crate::state::AppState;

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DiscoveredCamera {
    pub interface_index: i32,
    pub device_index: i32,
    pub interface_id: String,
    pub device_id: String,
    pub model_name: String,
    pub firmware_version: String,
    pub label: String,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DiscoveredFramegrabber {
    pub interface_index: i32,
    pub device_index: i32,
    pub stream_index: i32,
    pub interface_id: String,
    pub device_id: String,
    pub stream_id: String,
    pub model_name: String,
    pub label: String,
}

#[derive(Deserialize)]
pub struct MockCameraOptions {
    pub directory: String,
    pub interval_ms: u32,
    pub r#loop: bool,
}

fn map_camera(c: ffi::BridgeCamera) -> DiscoveredCamera {
    DiscoveredCamera {
        interface_index: c.interface_index,
        device_index: c.device_index,
        interface_id: c.interface_id,
        device_id: c.device_id,
        model_name: c.model_name,
        firmware_version: c.firmware_version,
        label: c.label,
    }
}

fn map_fg(f: ffi::BridgeFramegrabber) -> DiscoveredFramegrabber {
    DiscoveredFramegrabber {
        interface_index: f.interface_index,
        device_index: f.device_index,
        stream_index: f.stream_index,
        interface_id: f.interface_id,
        device_id: f.device_id,
        stream_id: f.stream_id,
        model_name: f.model_name,
        label: f.label,
    }
}

#[tauri::command]
pub async fn discover_cameras(
    state: tauri::State<'_, AppState>,
) -> Result<Vec<DiscoveredCamera>, String> {
    let v = state
        .backend
        .discover_cameras()
        .map_err(|e| e.to_string())?;
    Ok(v.into_iter().map(map_camera).collect())
}

#[tauri::command]
pub async fn discover_framegrabbers(
    state: tauri::State<'_, AppState>,
) -> Result<Vec<DiscoveredFramegrabber>, String> {
    let v = state
        .backend
        .discover_framegrabbers()
        .map_err(|e| e.to_string())?;
    Ok(v.into_iter().map(map_fg).collect())
}

#[tauri::command]
pub async fn connect_camera(
    state: tauri::State<'_, AppState>,
    interface_index: i32,
    device_index: i32,
    label: String,
) -> Result<(), String> {
    state
        .backend
        .set_hardware_camera(interface_index, device_index, &label)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn configure_mock(
    state: tauri::State<'_, AppState>,
    options: MockCameraOptions,
) -> Result<(), String> {
    state
        .backend
        .configure_mock(&options.directory, options.interval_ms, options.r#loop)
        .map_err(|e| e.to_string())
}
