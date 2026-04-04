use serde::Serialize;

#[derive(Serialize)]
pub struct FrameData {
    pub index: u64,
    pub width: u64,
    pub height: u64,
    pub image_base64: String,
    pub timestamp_ns: u64,
}

#[derive(Serialize)]
pub struct PlaybackRange {
    pub earliest: u64,
    pub latest: u64,
    pub count: u64,
}

#[tauri::command]
pub async fn fetch_latest_frame() -> Result<Option<FrameData>, String> {
    // TODO: Call C++ bridge -> backend_.playback().fetchLatest()
    Ok(None)
}

#[tauri::command]
pub async fn fetch_frame_by_index(index: u64) -> Result<Option<FrameData>, String> {
    // TODO: Call C++ bridge -> backend_.playback().fetchByIndex(index)
    Ok(None)
}

#[tauri::command]
pub async fn get_playback_range() -> Result<PlaybackRange, String> {
    // TODO: Call C++ bridge -> backend_.playback().queryRange()
    Ok(PlaybackRange {
        earliest: 0,
        latest: 0,
        count: 0,
    })
}
