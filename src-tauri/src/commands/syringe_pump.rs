use serde::{Deserialize, Serialize};

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

#[tauri::command]
pub async fn connect_pump(
    pump_id: String,
    com_port: i32,
    baud_rate: i32,
    modbus_address: u8,
) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.syringePump().connect(...)
    Ok(())
}

#[tauri::command]
pub async fn disconnect_pump(pump_id: String) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.syringePump().disconnect(...)
    Ok(())
}

#[tauri::command]
pub async fn set_pump_flow_rate(pump_id: String, rate: f64, unit: u16) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.syringePump().setFlowRate(...)
    Ok(())
}

#[tauri::command]
pub async fn set_pump_direction(pump_id: String, direction: String) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.syringePump().setDirection(...)
    Ok(())
}

#[tauri::command]
pub async fn start_pump(pump_id: String) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.syringePump().start(...)
    Ok(())
}

#[tauri::command]
pub async fn stop_pump(pump_id: String) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.syringePump().stop(...)
    Ok(())
}

#[tauri::command]
pub async fn purge_pump(pump_id: String, direction: String) -> Result<(), String> {
    // TODO: Call C++ bridge -> backend_.syringePump().purge(...)
    Ok(())
}

#[tauri::command]
pub async fn get_pump_status(pump_id: String) -> Result<PumpStatus, String> {
    // TODO: Call C++ bridge -> backend_.syringePump().getStatus(...)
    Ok(PumpStatus {
        connected: false,
        run_status: "stop".to_string(),
        current_flow_rate: 0.0,
        accumulated_volume: 0.0,
        min_flow_rate: 0.0,
        max_flow_rate: 0.0,
        stalled: false,
    })
}

#[tauri::command]
pub async fn get_pump_config(pump_id: String) -> Result<PumpConfig, String> {
    // TODO: Call C++ bridge -> backend_.syringePump().getConfig(...)
    Err("Not implemented".to_string())
}
