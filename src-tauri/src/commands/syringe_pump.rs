use serde::{Deserialize, Serialize};

use crate::state::AppState;

#[derive(Serialize, Deserialize)]
pub struct PumpStatus {
    pub connected: bool,
    pub run_status: String,
    pub current_flow_rate: f64,
    pub accumulated_volume: f64,
    pub min_flow_rate: f64,
    pub max_flow_rate: f64,
    pub stalled: bool,
}

#[derive(Serialize, Deserialize)]
pub struct PumpConfig {
    pub com_port: i32,
    pub baud_rate: i32,
    pub modbus_address: u8,
    pub flow_rate: f64,
    pub flow_rate_unit: u16,
    pub direction: String,
}

fn pump_id_to_backend_id(pump_id: &str) -> Result<i32, String> {
    match pump_id.trim().to_ascii_lowercase().as_str() {
        "sample" => Ok(0),
        "sheath" => Ok(1),
        _ => Err("Invalid pump id. Expected 'sample' or 'sheath'".to_string()),
    }
}

#[tauri::command]
pub async fn connect_pump(
    state: tauri::State<'_, AppState>,
    pump_id: String,
    com_port: i32,
    baud_rate: i32,
    modbus_address: u8,
) -> Result<(), String> {
    let id = pump_id_to_backend_id(&pump_id)?;
    state
        .backend
        .connect_pump(id, com_port, baud_rate, modbus_address)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn disconnect_pump(
    state: tauri::State<'_, AppState>,
    pump_id: String,
) -> Result<(), String> {
    let id = pump_id_to_backend_id(&pump_id)?;
    state.backend.disconnect_pump(id);
    Ok(())
}

#[tauri::command]
pub async fn set_pump_flow_rate(
    state: tauri::State<'_, AppState>,
    pump_id: String,
    rate: f64,
    unit: u16,
) -> Result<(), String> {
    let id = pump_id_to_backend_id(&pump_id)?;
    state
        .backend
        .set_pump_flow_rate(id, rate, unit)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn set_pump_direction(
    state: tauri::State<'_, AppState>,
    pump_id: String,
    direction: String,
) -> Result<(), String> {
    let id = pump_id_to_backend_id(&pump_id)?;
    state
        .backend
        .set_pump_direction(id, &direction)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn start_pump(state: tauri::State<'_, AppState>, pump_id: String) -> Result<(), String> {
    let id = pump_id_to_backend_id(&pump_id)?;
    state.backend.start_pump(id).map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn stop_pump(state: tauri::State<'_, AppState>, pump_id: String) -> Result<(), String> {
    let id = pump_id_to_backend_id(&pump_id)?;
    state.backend.stop_pump(id).map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn purge_pump(
    state: tauri::State<'_, AppState>,
    pump_id: String,
    direction: String,
) -> Result<(), String> {
    let id = pump_id_to_backend_id(&pump_id)?;
    state
        .backend
        .purge_pump(id, &direction)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn get_pump_status(
    state: tauri::State<'_, AppState>,
    pump_id: String,
) -> Result<PumpStatus, String> {
    let id = pump_id_to_backend_id(&pump_id)?;
    let status = state.backend.get_pump_status(id);
    Ok(PumpStatus {
        connected: status.connected,
        run_status: status.run_status,
        current_flow_rate: status.current_flow_rate,
        accumulated_volume: status.accumulated_volume,
        min_flow_rate: status.min_flow_rate,
        max_flow_rate: status.max_flow_rate,
        stalled: status.stalled,
    })
}

#[tauri::command]
pub async fn get_pump_config(
    state: tauri::State<'_, AppState>,
    pump_id: String,
) -> Result<PumpConfig, String> {
    let id = pump_id_to_backend_id(&pump_id)?;
    let config = state.backend.get_pump_config(id);
    Ok(PumpConfig {
        com_port: config.com_port,
        baud_rate: config.baud_rate,
        modbus_address: config.modbus_address,
        flow_rate: config.flow_rate,
        flow_rate_unit: config.flow_rate_unit,
        direction: config.direction,
    })
}
