use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize)]
pub struct AutofocusConfig {
    pub focus_setpoint: f64,
    pub focus_range: f64,
    pub voltage_step: f64,
    pub fine_voltage_step: f64,
    pub max_voltage: f64,
    pub min_voltage: f64,
    pub initial_voltage: f64,
    pub manual_voltage_step: f64,
    pub ring_ratio_stale_ms: i32,
    pub require_new_sample_per_step: bool,
    pub min_samples_per_step: i32,
    pub safe_shutdown_voltage: f64,
    pub focus_direction: bool,
}

#[tauri::command]
pub async fn connect_autofocus(
    com_port: i32,
    baud_rate: i32,
    device_address: u8,
) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.autofocus().connect(...)
    Ok(())
}

#[tauri::command]
pub async fn disconnect_autofocus() -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.autofocus().disconnect()
    Ok(())
}

#[tauri::command]
pub async fn set_autofocus_enabled(enabled: bool) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.autofocus().setEnabled(enabled)
    Ok(())
}

#[tauri::command]
pub async fn increase_voltage() -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.autofocus().increaseVoltage()
    Ok(())
}

#[tauri::command]
pub async fn decrease_voltage() -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.autofocus().decreaseVoltage()
    Ok(())
}

#[tauri::command]
pub async fn get_autofocus_config() -> Result<AutofocusConfig, String> {
    // TODO: Call C++ bridge -> backend_.autofocus().getConfig()
    Err("Not implemented".to_string())
}

#[tauri::command]
pub async fn set_autofocus_config(config: AutofocusConfig) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.autofocus().setConfig(config)
    Ok(())
}
