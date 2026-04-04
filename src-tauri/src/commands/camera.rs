use serde::{Deserialize, Serialize};

#[derive(Clone, Serialize)]
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

#[tauri::command]
pub async fn discover_cameras() -> Result<Vec<DiscoveredCamera>, String> {
    // TODO: Call C++ bridge -> backend_.cameraControl().discoverCameras()
    Ok(vec![])
}

#[tauri::command]
pub async fn discover_framegrabbers() -> Result<Vec<DiscoveredFramegrabber>, String> {
    // TODO: Call C++ bridge -> backend_.cameraControl().discoverFramegrabbers()
    Ok(vec![])
}

#[tauri::command]
pub async fn connect_camera(
    interface_index: i32,
    device_index: i32,
    label: String,
) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.setHardwareCameraSelection(...)
    Ok(())
}

#[tauri::command]
pub async fn configure_mock(options: MockCameraOptions) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.configureMockCamera(...)
    Ok(())
}
