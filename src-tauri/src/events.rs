use serde::Serialize;

/// Event names for backend → frontend push notifications
pub const FRAME_NEW: &str = "frame:new";
pub const STATS_UPDATE: &str = "stats:update";
pub const BACKGROUND_CAPTURED: &str = "background:captured";
pub const AUTOFOCUS_STATUS: &str = "autofocus:status";
pub const PUMP_STATUS: &str = "pump:status";

#[derive(Clone, Serialize)]
pub struct FrameNewPayload {
    pub index: u64,
    pub width: u64,
    pub height: u64,
    pub image_base64: String,
    pub timestamp_ns: u64,
}

#[derive(Clone, Serialize)]
pub struct StatsUpdatePayload {
    pub capture_frame_rate: f64,
    pub capture_data_rate_mbps: f64,
    pub algo_fps: f64,
    pub valid_fps: f64,
    pub invalid_fps: f64,
    pub algo_avg_us: f64,
    pub total_valid_flushed: u64,
}

#[derive(Clone, Serialize)]
pub struct BackgroundCapturedPayload {
    pub image_base64: String,
    pub frame_index: u64,
}

#[derive(Clone, Serialize)]
pub struct AutofocusStatusPayload {
    pub connected: bool,
    pub enabled: bool,
    pub voltage: f64,
    pub average_ring_ratio: f64,
    pub median_ring_ratio: f64,
    pub status_message: String,
}

#[derive(Clone, Serialize)]
pub struct PumpStatusPayload {
    pub pump_id: String,
    pub connected: bool,
    pub run_status: String,
    pub current_flow_rate: f64,
    pub accumulated_volume: f64,
    pub stalled: bool,
}
