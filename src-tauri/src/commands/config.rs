use crate::state::AppState;

#[tauri::command]
pub async fn get_app_config(state: tauri::State<'_, AppState>) -> Result<String, String> {
    Ok(state.backend.get_app_config())
}

#[tauri::command]
pub async fn set_app_config(state: tauri::State<'_, AppState>, json: String) -> Result<(), String> {
    state.backend.set_app_config(&json);
    Ok(())
}

#[tauri::command]
pub async fn apply_camera_script(
    state: tauri::State<'_, AppState>,
    path: String,
) -> Result<Option<String>, String> {
    let err = state.backend.apply_camera_script(&path);
    if err.trim().is_empty() {
        Ok(None)
    } else {
        Ok(Some(err))
    }
}

#[tauri::command]
pub async fn set_pixel_to_micron_factor(
    state: tauri::State<'_, AppState>,
    factor: f64,
) -> Result<(), String> {
    state.backend.set_pixel_to_micron_factor(factor);
    Ok(())
}

#[tauri::command]
pub async fn save_buffer_to_disk(
    state: tauri::State<'_, AppState>,
    output_dir: String,
    start_index: Option<u64>,
    end_index: Option<u64>,
) -> Result<(), String> {
    match (start_index, end_index) {
        (Some(start), Some(end)) => state
            .backend
            .save_buffer_to_disk(&output_dir, start, end, true)
            .map_err(|e| e.to_string()),
        _ => state
            .backend
            .save_buffer_to_disk(&output_dir, 0, 0, false)
            .map_err(|e| e.to_string()),
    }
}
