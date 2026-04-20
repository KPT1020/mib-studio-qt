use serde::{Deserialize, Serialize};

use crate::bridge::ffi;
use crate::state::AppState;

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
    pub brightness: BrightnessQuantiles,
    pub is_target_group: bool,
}

#[derive(Serialize)]
pub struct BrightnessQuantiles {
    pub q1: f64,
    pub q2: f64,
    pub q3: f64,
    pub q4: f64,
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

fn map_processing_config(c: ffi::BridgeProcessingConfig) -> ProcessingConfig {
    ProcessingConfig {
        gaussian_blur_size: c.gaussian_blur_size,
        bg_subtract_threshold: c.bg_subtract_threshold,
        morph_kernel_size: c.morph_kernel_size,
        morph_iterations: c.morph_iterations,
        area_threshold_min: c.area_threshold_min,
        area_threshold_max: c.area_threshold_max,
        deformability_threshold_min: c.deformability_threshold_min,
        deformability_threshold_max: c.deformability_threshold_max,
        enable_border_check: c.enable_border_check,
        enable_area_range_check: c.enable_area_range_check,
        enable_deformability_range_check: c.enable_deformability_range_check,
        area_ratio_threshold_max: c.area_ratio_threshold_max,
        enable_area_ratio_check: c.enable_area_ratio_check,
        require_single_inner_contour: c.require_single_inner_contour,
        empty_frame_pixel_threshold: c.empty_frame_pixel_threshold,
        auto_background_enabled: c.auto_background_enabled,
        auto_background_empty_frames: c.auto_background_empty_frames,
        auto_background_cooldown_frames: c.auto_background_cooldown_frames,
        enable_target_group: c.enable_target_group,
        target_group_area_min: c.target_group_area_min,
        target_group_area_max: c.target_group_area_max,
        target_group_deformability_min: c.target_group_deformability_min,
        target_group_deformability_max: c.target_group_deformability_max,
        enable_target_group_emodulus: c.enable_target_group_emodulus,
        target_group_emodulus_min: c.target_group_emodulus_min,
        target_group_emodulus_max: c.target_group_emodulus_max,
        multi_image_enabled: c.multi_image_enabled,
        multi_image_count: c.multi_image_count,
    }
}

fn map_bridge_processing_config(
    config: ProcessingConfig,
    current: ffi::BridgeProcessingConfig,
) -> ffi::BridgeProcessingConfig {
    ffi::BridgeProcessingConfig {
        gaussian_blur_size: config.gaussian_blur_size,
        bg_subtract_threshold: config.bg_subtract_threshold,
        morph_kernel_size: config.morph_kernel_size,
        morph_iterations: config.morph_iterations,
        area_threshold_min: config.area_threshold_min,
        area_threshold_max: config.area_threshold_max,
        deformability_threshold_min: config.deformability_threshold_min,
        deformability_threshold_max: config.deformability_threshold_max,
        enable_border_check: config.enable_border_check,
        enable_area_range_check: config.enable_area_range_check,
        enable_deformability_range_check: config.enable_deformability_range_check,
        area_ratio_threshold_max: config.area_ratio_threshold_max,
        enable_area_ratio_check: config.enable_area_ratio_check,
        ring_ratio_min: current.ring_ratio_min,
        ring_ratio_max: current.ring_ratio_max,
        enable_ring_ratio_check: current.enable_ring_ratio_check,
        require_single_inner_contour: config.require_single_inner_contour,
        empty_frame_pixel_threshold: config.empty_frame_pixel_threshold,
        auto_background_enabled: config.auto_background_enabled,
        auto_background_empty_frames: config.auto_background_empty_frames,
        auto_background_cooldown_frames: config.auto_background_cooldown_frames,
        enable_target_group: config.enable_target_group,
        target_group_area_min: config.target_group_area_min,
        target_group_area_max: config.target_group_area_max,
        target_group_deformability_min: config.target_group_deformability_min,
        target_group_deformability_max: config.target_group_deformability_max,
        enable_target_group_emodulus: config.enable_target_group_emodulus,
        target_group_emodulus_min: config.target_group_emodulus_min,
        target_group_emodulus_max: config.target_group_emodulus_max,
        multi_image_enabled: config.multi_image_enabled,
        multi_image_count: config.multi_image_count,
    }
}

fn map_filter_result(f: ffi::BridgeFilterResult) -> FilterResult {
    FilterResult {
        is_valid: f.is_valid,
        touches_border: f.touches_border,
        has_single_inner_contour: f.has_single_inner_contour,
        in_range: f.in_range,
        inner_contour_count: f.inner_contour_count,
        deformability: f.deformability,
        area: f.area,
        area_ratio: f.area_ratio,
        ring_ratio: f.ring_ratio,
        youngs_modulus: f.youngs_modulus,
        brightness: BrightnessQuantiles {
            q1: f.brightness.q1,
            q2: f.brightness.q2,
            q3: f.brightness.q3,
            q4: f.brightness.q4,
        },
        is_target_group: f.is_target_group,
    }
}

fn map_processed_frame(f: ffi::BridgeProcessedFrame) -> ProcessedFrame {
    ProcessedFrame {
        index: f.index,
        timestamp_ns: f.timestamp_ns,
        image_base64: f.image_base64,
        image_width: f.image_width,
        image_height: f.image_height,
        validation: map_filter_result(f.validation),
    }
}

#[tauri::command]
pub async fn get_processing_config(
    state: tauri::State<'_, AppState>,
) -> Result<ProcessingConfig, String> {
    Ok(map_processing_config(state.backend.get_processing_config()))
}

#[tauri::command]
pub async fn set_processing_config(
    state: tauri::State<'_, AppState>,
    config: ProcessingConfig,
) -> Result<(), String> {
    let current = state.backend.get_processing_config();
    state
        .backend
        .set_processing_config(&map_bridge_processing_config(config, current));
    Ok(())
}

#[tauri::command]
pub async fn set_realtime_roi(
    state: tauri::State<'_, AppState>,
    roi: Option<Roi>,
) -> Result<(), String> {
    if let Some(roi) = roi {
        let mapped = ffi::BridgeRoi {
            x: roi.x,
            y: roi.y,
            w: roi.w,
            h: roi.h,
        };
        state.backend.set_realtime_roi(&mapped);
    } else {
        state.backend.clear_realtime_roi();
    }
    Ok(())
}

#[tauri::command]
pub async fn set_realtime_background(state: tauri::State<'_, AppState>) -> Result<(), String> {
    if !state.backend.set_realtime_background() {
        return Err("Failed to set realtime background from latest frame".to_string());
    }
    Ok(())
}

#[tauri::command]
pub async fn get_monitoring_frames(
    state: tauri::State<'_, AppState>,
) -> Result<MonitoringFrames, String> {
    let frames = state.backend.get_monitoring_frames();
    Ok(MonitoringFrames {
        valid: frames.valid.into_iter().map(map_processed_frame).collect(),
        invalid: frames
            .invalid
            .into_iter()
            .map(map_processed_frame)
            .collect(),
    })
}

#[tauri::command]
pub async fn clear_monitoring_frames(state: tauri::State<'_, AppState>) -> Result<(), String> {
    state.backend.clear_monitoring_frames();
    Ok(())
}
