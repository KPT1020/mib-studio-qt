use super::processing::ProcessedFrame;
use crate::bridge::ffi;
use crate::state::AppState;

fn map_filter_result(f: ffi::BridgeFilterResult) -> super::processing::FilterResult {
    super::processing::FilterResult {
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
        brightness: super::processing::BrightnessQuantiles {
            q1: f.brightness.q1,
            q2: f.brightness.q2,
            q3: f.brightness.q3,
            q4: f.brightness.q4,
        },
        is_target_group: f.is_target_group,
    }
}

fn map_processed_frame(frame: ffi::BridgeProcessedFrame) -> ProcessedFrame {
    ProcessedFrame {
        index: frame.index,
        timestamp_ns: frame.timestamp_ns,
        image_base64: frame.image_base64,
        image_width: frame.image_width,
        image_height: frame.image_height,
        validation: map_filter_result(frame.validation),
    }
}

#[tauri::command]
pub async fn start_experiment(
    state: tauri::State<'_, AppState>,
    hdf5_path: String,
) -> Result<(), String> {
    state
        .backend
        .start_experiment(&hdf5_path)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn stop_experiment(state: tauri::State<'_, AppState>) -> Result<(), String> {
    state.backend.stop_experiment().map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn load_hdf5_file(state: tauri::State<'_, AppState>, path: String) -> Result<(), String> {
    state
        .backend
        .load_hdf5_file(&path)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn get_hdf5_valid_frames(
    state: tauri::State<'_, AppState>,
) -> Result<Vec<ProcessedFrame>, String> {
    Ok(state
        .backend
        .get_hdf5_valid_frames()
        .into_iter()
        .map(map_processed_frame)
        .collect())
}

#[tauri::command]
pub async fn get_hdf5_invalid_frames(
    state: tauri::State<'_, AppState>,
) -> Result<Vec<ProcessedFrame>, String> {
    Ok(state
        .backend
        .get_hdf5_invalid_frames()
        .into_iter()
        .map(map_processed_frame)
        .collect())
}

#[tauri::command]
pub async fn export_metrics_csv(
    state: tauri::State<'_, AppState>,
    hdf5_path: String,
    output_path: String,
) -> Result<(), String> {
    state
        .backend
        .export_metrics_csv(&hdf5_path, &output_path)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn start_frame_recording(
    state: tauri::State<'_, AppState>,
    hdf5_path: String,
) -> Result<(), String> {
    state
        .backend
        .start_frame_recording(&hdf5_path)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn stop_frame_recording(state: tauri::State<'_, AppState>) -> Result<(), String> {
    state.backend.stop_frame_recording();
    Ok(())
}
