#[tauri::command]
pub async fn start_capture() -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.capture().start()
    Ok(())
}

#[tauri::command]
pub async fn stop_capture() -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.capture().stop()
    Ok(())
}

#[tauri::command]
pub async fn get_capture_running() -> Result<bool, String> {
    // TODO: Call C++ bridge -> backend_.capture().isRunning()
    Ok(false)
}
