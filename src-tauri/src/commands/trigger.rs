use crate::state::AppState;

#[tauri::command]
pub async fn fire_sort_trigger(state: tauri::State<'_, AppState>) -> Result<(), String> {
    state.backend.fire_sort_trigger();
    Ok(())
}

#[tauri::command]
pub async fn set_trigger_duration(
    state: tauri::State<'_, AppState>,
    duration_us: i32,
) -> Result<(), String> {
    state.backend.set_trigger_duration(duration_us);
    Ok(())
}
