import { invoke } from "@tauri-apps/api/core";
import type {
  DiscoveredCamera,
  DiscoveredFramegrabber,
  ProcessingConfig,
  MockCameraOptions,
  Roi,
  PlaybackRange,
  FrameData,
  ProcessedFrame,
  AutofocusConfig,
  PumpId,
  PumpDirection,
  PumpStatus,
  PumpConfig,
} from "../types/backend";

// ---- Camera Control ----

export async function discoverCameras(): Promise<DiscoveredCamera[]> {
  return invoke("discover_cameras");
}

export async function discoverFramegrabbers(): Promise<DiscoveredFramegrabber[]> {
  return invoke("discover_framegrabbers");
}

export async function connectCamera(
  interfaceIndex: number,
  deviceIndex: number,
  label: string
): Promise<void> {
  return invoke("connect_camera", { interfaceIndex, deviceIndex, label });
}

export async function configureMock(options: MockCameraOptions): Promise<void> {
  return invoke("configure_mock", { options });
}

// ---- Capture ----

export async function startCapture(): Promise<void> {
  return invoke("start_capture");
}

export async function stopCapture(): Promise<void> {
  return invoke("stop_capture");
}

export async function getCaptureRunning(): Promise<boolean> {
  return invoke("get_capture_running");
}

// ---- Playback ----

export async function fetchLatestFrame(): Promise<FrameData | null> {
  return invoke("fetch_latest_frame");
}

export async function fetchFrameByIndex(index: number): Promise<FrameData | null> {
  return invoke("fetch_frame_by_index", { index });
}

export async function getPlaybackRange(): Promise<PlaybackRange> {
  return invoke("get_playback_range");
}

// ---- Processing ----

export async function getProcessingConfig(): Promise<ProcessingConfig> {
  return invoke("get_processing_config");
}

export async function setProcessingConfig(config: ProcessingConfig): Promise<void> {
  return invoke("set_processing_config", { config });
}

export async function setRealtimeRoi(roi: Roi | null): Promise<void> {
  return invoke("set_realtime_roi", { roi });
}

export async function setRealtimeBackground(): Promise<void> {
  return invoke("set_realtime_background");
}

export async function getMonitoringFrames(): Promise<{
  valid: ProcessedFrame[];
  invalid: ProcessedFrame[];
}> {
  return invoke("get_monitoring_frames");
}

export async function clearMonitoringFrames(): Promise<void> {
  return invoke("clear_monitoring_frames");
}

// ---- Experiment / HDF5 ----

export async function startExperiment(hdf5Path: string): Promise<void> {
  return invoke("start_experiment", { hdf5Path });
}

export async function stopExperiment(): Promise<void> {
  return invoke("stop_experiment");
}

export async function loadHdf5File(path: string): Promise<void> {
  return invoke("load_hdf5_file", { path });
}

export async function getHdf5ValidFrames(): Promise<ProcessedFrame[]> {
  return invoke("get_hdf5_valid_frames");
}

export async function getHdf5InvalidFrames(): Promise<ProcessedFrame[]> {
  return invoke("get_hdf5_invalid_frames");
}

export async function exportMetricsCsv(hdf5Path: string, outputPath: string): Promise<void> {
  return invoke("export_metrics_csv", { hdf5Path, outputPath });
}

// ---- Recording ----

export async function startFrameRecording(hdf5Path: string): Promise<void> {
  return invoke("start_frame_recording", { hdf5Path });
}

export async function stopFrameRecording(): Promise<void> {
  return invoke("stop_frame_recording");
}

// ---- Autofocus ----

export async function connectAutofocus(
  comPort: number,
  baudRate: number,
  deviceAddress: number
): Promise<void> {
  return invoke("connect_autofocus", { comPort, baudRate, deviceAddress });
}

export async function disconnectAutofocus(): Promise<void> {
  return invoke("disconnect_autofocus");
}

export async function setAutofocusEnabled(enabled: boolean): Promise<void> {
  return invoke("set_autofocus_enabled", { enabled });
}

export async function increaseVoltage(): Promise<void> {
  return invoke("increase_voltage");
}

export async function decreaseVoltage(): Promise<void> {
  return invoke("decrease_voltage");
}

export async function getAutofocusConfig(): Promise<AutofocusConfig> {
  return invoke("get_autofocus_config");
}

export async function setAutofocusConfig(config: AutofocusConfig): Promise<void> {
  return invoke("set_autofocus_config", { config });
}

// ---- Syringe Pump ----

export async function connectPump(
  pumpId: PumpId,
  comPort: number,
  baudRate: number,
  modbusAddress: number
): Promise<void> {
  return invoke("connect_pump", { pumpId, comPort, baudRate, modbusAddress });
}

export async function disconnectPump(pumpId: PumpId): Promise<void> {
  return invoke("disconnect_pump", { pumpId });
}

export async function setPumpFlowRate(
  pumpId: PumpId,
  rate: number,
  unit: number
): Promise<void> {
  return invoke("set_pump_flow_rate", { pumpId, rate, unit });
}

export async function setPumpDirection(
  pumpId: PumpId,
  direction: PumpDirection
): Promise<void> {
  return invoke("set_pump_direction", { pumpId, direction });
}

export async function startPump(pumpId: PumpId): Promise<void> {
  return invoke("start_pump", { pumpId });
}

export async function stopPump(pumpId: PumpId): Promise<void> {
  return invoke("stop_pump", { pumpId });
}

export async function purgePump(pumpId: PumpId, direction: PumpDirection): Promise<void> {
  return invoke("purge_pump", { pumpId, direction });
}

export async function getPumpStatus(pumpId: PumpId): Promise<PumpStatus> {
  return invoke("get_pump_status", { pumpId });
}

export async function getPumpConfig(pumpId: PumpId): Promise<PumpConfig> {
  return invoke("get_pump_config", { pumpId });
}

// ---- Trigger ----

export async function fireSortTrigger(): Promise<void> {
  return invoke("fire_sort_trigger");
}

export async function setTriggerDuration(durationUs: number): Promise<void> {
  return invoke("set_trigger_duration", { durationUs });
}

// ---- Config ----

export async function getAppConfig(): Promise<string> {
  return invoke("get_app_config");
}

export async function setAppConfig(json: string): Promise<void> {
  return invoke("set_app_config", { json });
}

export async function applyCameraScript(path: string): Promise<string | null> {
  return invoke("apply_camera_script", { path });
}

export async function setPixelToMicronFactor(factor: number): Promise<void> {
  return invoke("set_pixel_to_micron_factor", { factor });
}

// ---- Buffer Save ----

export async function saveBufferToDisk(
  outputDir: string,
  startIndex?: number,
  endIndex?: number
): Promise<void> {
  return invoke("save_buffer_to_disk", { outputDir, startIndex, endIndex });
}
