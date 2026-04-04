#[tauri::command]
pub async fn get_app_config() -> Result<String, String> {
    // TODO: Call C++ bridge -> backend_.getLastConfigJson()
    Ok("{}".to_string())
}

#[tauri::command]
pub async fn set_app_config(json: String) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.setLastConfigJson(json)
    Ok(())
}

#[tauri::command]
pub async fn apply_camera_script(path: String) -> Result<Option<String>, String> {
    // TODO: Call C++ bridge -> backend_.applyCameraScriptFromFile(path)
    Ok(None)
}

#[tauri::command]
pub async fn set_pixel_to_micron_factor(factor: f64) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.processing().setPixelToMicronFactor(factor)
    Ok(())
}

#[tauri::command]
pub async fn save_buffer_to_disk(
    output_dir: String,
    start_index: Option<u64>,
    end_index: Option<u64>,
) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.playback().saveFramesToDisk(...)
    Ok(())
}
