use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize)]
pub struct ProcessingConfig {
    pub gaussian_blur_size: i32,
    pub bg_subtract_threshold: i32,
    pub morph_kernel_size: i32,
    pub morph_iterations: i32,
    pub area_threshold_min: i32,
    pub area_threshold_max: i32,
    pub deformability_threshold_min: f64,
    pub deformability_threshold_max: f64,
    pub enable_border_check: bool,
    pub enable_area_range_check: bool,
    pub enable_deformability_range_check: bool,
    pub area_ratio_threshold_max: f64,
    pub enable_area_ratio_check: bool,
    pub require_single_inner_contour: bool,
    pub empty_frame_pixel_threshold: i32,
    pub auto_background_enabled: bool,
    pub auto_background_empty_frames: i32,
    pub auto_background_cooldown_frames: i32,
    pub enable_target_group: bool,
    pub target_group_area_min: i32,
    pub target_group_area_max: i32,
    pub target_group_deformability_min: f64,
    pub target_group_deformability_max: f64,
    pub enable_target_group_emodulus: bool,
    pub target_group_emodulus_min: f64,
    pub target_group_emodulus_max: f64,
    pub multi_image_enabled: bool,
    pub multi_image_count: i32,
}

#[derive(Serialize, Deserialize)]
pub struct Roi {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
}

#[derive(Serialize)]
pub struct FilterResult {
    pub is_valid: bool,
    pub touches_border: bool,
    pub has_single_inner_contour: bool,
    pub in_range: bool,
    pub inner_contour_count: i32,
    pub deformability: f64,
    pub area: f64,
    pub area_ratio: f64,
    pub ring_ratio: f64,
    pub youngs_modulus: f64,
    pub is_target_group: bool,
}

#[derive(Serialize)]
pub struct ProcessedFrame {
    pub index: u64,
    pub timestamp_ns: u64,
    pub image_base64: String,
    pub image_width: u32,
    pub image_height: u32,
    pub validation: FilterResult,
}

#[derive(Serialize)]
pub struct MonitoringFrames {
    pub valid: Vec<ProcessedFrame>,
    pub invalid: Vec<ProcessedFrame>,
}

#[tauri::command]
pub async fn get_processing_config() -> Result<ProcessingConfig, String> {
    // TODO: Call C++ bridge -> backend_.processing().getProcessingConfig()
    Err("Not implemented".to_string())
}

#[tauri::command]
pub async fn set_processing_config(config: ProcessingConfig) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.processing().setProcessingConfig(config)
    Ok(())
}

#[tauri::command]
pub async fn set_realtime_roi(roi: Option<Roi>) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.processing().setRealtimeRoi(roi)
    Ok(())
}

#[tauri::command]
pub async fn set_realtime_background() -> Result<(), String> {
    // TODO: Capture current frame as background
    Ok(())
}

#[tauri::command]
pub async fn get_monitoring_frames() -> Result<MonitoringFrames, String> {
    // TODO: Call C++ bridge -> getMonitoringValid/InvalidFrames()
    Ok(MonitoringFrames {
        valid: vec![],
        invalid: vec![],
    })
}

#[tauri::command]
pub async fn clear_monitoring_frames() -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.processing().clearMonitoringFrames()
    Ok(())
}
