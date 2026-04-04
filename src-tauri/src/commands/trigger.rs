#[tauri::command]
pub async fn fire_sort_trigger() -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.trigger().pulse()
    Ok(())
}

#[tauri::command]
pub async fn set_trigger_duration(duration_us: i32) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.trigger().setPulseDurationUs(duration_us)
    Ok(())
}
