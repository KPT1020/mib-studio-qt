// Prevents additional console window on Windows in release
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod events;

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .invoke_handler(tauri::generate_handler![
            // Camera control
            commands::camera::discover_cameras,
            commands::camera::discover_framegrabbers,
            commands::camera::connect_camera,
            commands::camera::configure_mock,
            // Capture
            commands::capture::start_capture,
            commands::capture::stop_capture,
            commands::capture::get_capture_running,
            // Playback
            commands::playback::fetch_latest_frame,
            commands::playback::fetch_frame_by_index,
            commands::playback::get_playback_range,
            // Processing
            commands::processing::get_processing_config,
            commands::processing::set_processing_config,
            commands::processing::set_realtime_roi,
            commands::processing::set_realtime_background,
            commands::processing::get_monitoring_frames,
            commands::processing::clear_monitoring_frames,
            // HDF5
            commands::hdf5::start_experiment,
            commands::hdf5::stop_experiment,
            commands::hdf5::load_hdf5_file,
            commands::hdf5::get_hdf5_valid_frames,
            commands::hdf5::get_hdf5_invalid_frames,
            commands::hdf5::export_metrics_csv,
            commands::hdf5::start_frame_recording,
            commands::hdf5::stop_frame_recording,
            // Autofocus
            commands::autofocus::connect_autofocus,
            commands::autofocus::disconnect_autofocus,
            commands::autofocus::set_autofocus_enabled,
            commands::autofocus::increase_voltage,
            commands::autofocus::decrease_voltage,
            commands::autofocus::get_autofocus_config,
            commands::autofocus::set_autofocus_config,
            // Syringe pump
            commands::syringe_pump::connect_pump,
            commands::syringe_pump::disconnect_pump,
            commands::syringe_pump::set_pump_flow_rate,
            commands::syringe_pump::set_pump_direction,
            commands::syringe_pump::start_pump,
            commands::syringe_pump::stop_pump,
            commands::syringe_pump::purge_pump,
            commands::syringe_pump::get_pump_status,
            commands::syringe_pump::get_pump_config,
            // Trigger
            commands::trigger::fire_sort_trigger,
            commands::trigger::set_trigger_duration,
            // Config
            commands::config::get_app_config,
            commands::config::set_app_config,
            commands::config::apply_camera_script,
            commands::config::set_pixel_to_micron_factor,
            commands::config::save_buffer_to_disk,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
