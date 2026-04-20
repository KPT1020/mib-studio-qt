use base64::Engine;
use serde::Serialize;

use crate::state::AppState;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FrameData {
    pub index: u64,
    pub width: u64,
    pub height: u64,
    pub image_base64: String,
    pub timestamp_ns: u64,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PlaybackRange {
    pub earliest: u64,
    pub latest: u64,
    pub count: u64,
}

#[tauri::command]
pub async fn fetch_latest_frame(
    state: tauri::State<'_, AppState>,
) -> Result<Option<FrameData>, String> {
    let png = state.backend.fetch_latest_frame_png();
    if png.is_empty() {
        return Ok(None);
    }
    let meta = state.backend.fetch_latest_frame_meta();
    if meta.width == 0 {
        return Ok(None);
    }
    let image_base64 = base64::engine::general_purpose::STANDARD.encode(&png);
    Ok(Some(FrameData {
        index: meta.index,
        width: meta.width,
        height: meta.height,
        image_base64,
        timestamp_ns: meta.timestamp_ns,
    }))
}

#[tauri::command]
pub async fn fetch_frame_by_index(
    _state: tauri::State<'_, AppState>,
    _index: u64,
) -> Result<Option<FrameData>, String> {
    // Deferred: bridge would expose fetch_by_index PNG + meta.
    Ok(None)
}

#[tauri::command]
pub async fn get_playback_range(
    state: tauri::State<'_, AppState>,
) -> Result<PlaybackRange, String> {
    let r = state.backend.get_playback_range();
    Ok(PlaybackRange {
        earliest: r.earliest,
        latest: r.latest,
        count: r.count,
    })
}
