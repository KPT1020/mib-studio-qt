use crate::state::AppState;

#[tauri::command]
pub async fn start_capture(state: tauri::State<'_, AppState>) -> Result<(), String> {
    state.backend.start_capture().map_err(|e| e.to_string())?;
    Ok(())
}

#[tauri::command]
pub async fn stop_capture(state: tauri::State<'_, AppState>) -> Result<(), String> {
    state.backend.stop_capture();
    Ok(())
}

#[tauri::command]
pub async fn get_capture_running(state: tauri::State<'_, AppState>) -> Result<bool, String> {
    Ok(state.backend.is_capture_running())
}
