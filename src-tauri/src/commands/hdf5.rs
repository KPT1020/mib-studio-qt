use super::processing::ProcessedFrame;

#[tauri::command]
pub async fn start_experiment(hdf5_path: String) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.hdf5().openFile(path)
    Ok(())
}

#[tauri::command]
pub async fn stop_experiment() -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.hdf5().closeFile()
    Ok(())
}

#[tauri::command]
pub async fn load_hdf5_file(path: String) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.hdf5().loadFile(path)
    Ok(())
}

#[tauri::command]
pub async fn get_hdf5_valid_frames() -> Result<Vec<ProcessedFrame>, String> {
    // TODO: Call C++ bridge -> backend_.hdf5().readValidFrames()
    Ok(vec![])
}

#[tauri::command]
pub async fn get_hdf5_invalid_frames() -> Result<Vec<ProcessedFrame>, String> {
    // TODO: Call C++ bridge -> backend_.hdf5().readInvalidFrames()
    Ok(vec![])
}

#[tauri::command]
pub async fn export_metrics_csv(hdf5_path: String, output_path: String) -> Result<(), String> {
    // TODO: Export metrics to CSV
    Ok(())
}

#[tauri::command]
pub async fn start_frame_recording(hdf5_path: String) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.startFrameRecording(path)
    Ok(())
}

#[tauri::command]
pub async fn stop_frame_recording() -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.stopFrameRecording()
    Ok(())
}
