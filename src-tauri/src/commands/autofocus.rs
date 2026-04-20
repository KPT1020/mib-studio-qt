use serde::{Deserialize, Serialize};

use crate::bridge::ffi;
use crate::state::AppState;

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
    state: tauri::State<'_, AppState>,
    com_port: i32,
    baud_rate: i32,
    device_address: u8,
) -> Result<(), String> {
    state
        .backend
        .connect_autofocus(com_port, baud_rate, device_address)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn disconnect_autofocus(state: tauri::State<'_, AppState>) -> Result<(), String> {
    state.backend.disconnect_autofocus();
    Ok(())
}

#[tauri::command]
pub async fn set_autofocus_enabled(
    state: tauri::State<'_, AppState>,
    enabled: bool,
) -> Result<(), String> {
    state.backend.set_autofocus_enabled(enabled);
    Ok(())
}

#[tauri::command]
pub async fn increase_voltage(state: tauri::State<'_, AppState>) -> Result<(), String> {
    state.backend.increase_voltage();
    Ok(())
}

#[tauri::command]
pub async fn decrease_voltage(state: tauri::State<'_, AppState>) -> Result<(), String> {
    state.backend.decrease_voltage();
    Ok(())
}

#[tauri::command]
pub async fn get_autofocus_config(
    state: tauri::State<'_, AppState>,
) -> Result<AutofocusConfig, String> {
    let c = state.backend.get_autofocus_config();
    Ok(AutofocusConfig {
        focus_setpoint: c.focus_setpoint,
        focus_range: c.focus_range,
        voltage_step: c.voltage_step,
        fine_voltage_step: c.fine_voltage_step,
        max_voltage: c.max_voltage,
        min_voltage: c.min_voltage,
        initial_voltage: c.initial_voltage,
        manual_voltage_step: c.manual_voltage_step,
        ring_ratio_stale_ms: c.ring_ratio_stale_ms,
        require_new_sample_per_step: c.require_new_sample_per_step,
        min_samples_per_step: c.min_samples_per_step,
        safe_shutdown_voltage: c.safe_shutdown_voltage,
        focus_direction: c.focus_direction,
    })
}

#[tauri::command]
pub async fn set_autofocus_config(
    state: tauri::State<'_, AppState>,
    config: AutofocusConfig,
) -> Result<(), String> {
    let mapped = ffi::BridgeAutofocusConfig {
        focus_setpoint: config.focus_setpoint,
        focus_range: config.focus_range,
        voltage_step: config.voltage_step,
        fine_voltage_step: config.fine_voltage_step,
        max_voltage: config.max_voltage,
        min_voltage: config.min_voltage,
        initial_voltage: config.initial_voltage,
        manual_voltage_step: config.manual_voltage_step,
        ring_ratio_stale_ms: config.ring_ratio_stale_ms,
        require_new_sample_per_step: config.require_new_sample_per_step,
        min_samples_per_step: config.min_samples_per_step,
        safe_shutdown_voltage: config.safe_shutdown_voltage,
        focus_direction: config.focus_direction,
    };
    state.backend.set_autofocus_config(&mapped);
    Ok(())
}
