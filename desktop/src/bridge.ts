// Typed client for the Tauri command layer that wraps the Rust ↔ C++ bridge
// (mib-bridge, ADR 0003). Mirrors the DTOs in src-tauri/src/lib.rs.
import { invoke } from "@tauri-apps/api/core";

export interface CmdResult {
  ok: boolean;
  command: number;
  message: string;
  /** Non-zero when the command started/targeted a tracked long-running
   *  operation (bridge schema v4); correlates with OperationStatus events. */
  operation_id: number;
}

export interface FrameMeta {
  valid: boolean;
  frame_index: number;
  timestamp_ns: number;
  width: number;
  height: number;
  pixel_format: number;
  stride_bytes: number;
  byte_len: number;
}

export interface ProcessingStats {
  valid: boolean;
  algo_fps1s: number;
  valid_fps1s: number;
  invalid_fps1s: number;
  pixel_to_micron: number;
}

/** Experiment lifecycle snapshot (bridge schema v5, BE-4). `state` is a
 *  contract EXPERIMENT_STATES value. */
export interface ExperimentStatus {
  valid: boolean;
  state: number;
  start_time_ns: number;
  end_time_ns: number;
  valid_buffered: number;
  invalid_buffered: number;
  valid_saved: number;
  invalid_saved: number;
  dropped_valid: number;
  dropped_invalid: number;
  flushing: boolean;
  cancelled: boolean;
  output_path: string;
  message: string;
}

/** Per-dataset capabilities of the loaded review file (schema v9, BE-6). */
export interface ReviewDatasetInfo {
  present: boolean;
  count: number;
  height: number;
  width: number;
  channels: number;
}

/** Review metadata of the loaded HDF5 file (schema v9, BE-6). */
export interface ReviewMetadata {
  valid: boolean;
  file_open: boolean;
  recording_file: boolean;
  start_time_ns: number;
  end_time_ns: number;
  total_valid: number;
  total_invalid: number;
  roi_x: number;
  roi_y: number;
  roi_w: number;
  roi_h: number;
  has_background: boolean;
  has_core_identity: boolean;
  core_version: string;
  core_source: string;
  core_release_tag: string;
  valid_images: ReviewDatasetInfo;
  invalid_images: ReviewDatasetInfo;
  valid_masks: ReviewDatasetInfo;
  invalid_masks: ReviewDatasetInfo;
  recorded_images: ReviewDatasetInfo;
  file_path: string;
}

/** One page of review metrics (schema v9, BE-6). */
export interface ReviewMetricsPage {
  valid: boolean;
  total: number;
  offset: number;
  rows: MonitoringRow[];
}

/** Processing-core identity/pin status (bridge schema v8, BE-3). */
export interface ProcessingCoreStatus {
  valid: boolean;
  active_version: string;
  contract_version: number;
  engine_abi_version: number;
  source: string;
  release_tag: string;
  build_id: string;
  artifact_sha256: string;
  required_version: string;
  pin_satisfied: boolean;
}

/** One discovered camera (bridge schema v7, BE-2). `camera_type` is a
 *  contract CAMERA_TYPES value (0 EGrabber, 1 MindVision, 2 Mock). */
export interface DiscoveredCamera {
  camera_type: number;
  camera_index: number;
  interface_index: number;
  device_index: number;
  interface_id: string;
  device_id: string;
  model_name: string;
  firmware_version: string;
  label: string;
}

export interface DiscoveredFramegrabber {
  interface_index: number;
  device_index: number;
  stream_index: number;
  interface_id: string;
  device_id: string;
  stream_id: string;
  model_name: string;
  label: string;
}

export interface CameraDiscovery {
  valid: boolean;
  cameras: DiscoveredCamera[];
  framegrabbers: DiscoveredFramegrabber[];
}

/** Authoritative selected-device snapshot (bridge schema v7, BE-2). `mode`
 *  is a contract CAMERA_SELECTION_MODES value. */
export interface CameraSelection {
  valid: boolean;
  mode: number;
  interface_index: number;
  device_index: number;
  label: string;
  mindvision_index: number;
  mindvision_config_path: string;
  camera_script_path: string;
  mock_frame_dir: string;
  mock_interval_ms: number;
  mock_loop: boolean;
  configured: boolean;
  running: boolean;
}

/** One monitoring metric row (bridge schema v6, BE-5). `(frame_index,
 *  object_id)` is a stable identity for reconciliation. */
export interface MonitoringRow {
  frame_index: number;
  timestamp_ns: number;
  valid: boolean;
  target_group: boolean;
  object_id: number;
  object_count: number;
  track_id: number;
  centroid_x: number;
  centroid_y: number;
  area: number;
  deformability: number;
  area_ratio: number;
  ring_ratio: number;
  youngs_modulus: number;
}

/** Bounded monitoring snapshot (bridge schema v6, BE-5). Evicted rows are
 *  observable as `*_appended - *_held`. */
export interface MonitoringSnapshot {
  valid: boolean;
  monitoring_active: boolean;
  valid_held: number;
  invalid_held: number;
  valid_appended: number;
  invalid_appended: number;
  capacity: number;
  latest_timestamp_ns: number;
  rows: MonitoringRow[];
}

/** Sorter trigger status snapshot (bridge schema v6, BE-5). */
export interface TriggerStatus {
  valid: boolean;
  camera_attached: boolean;
  pulse_duration_us: number;
  trigger_count: number;
  last_onset_us: number;
  last_object_id: number;
  last_track_id: number;
  periodic_active: boolean;
  periodic_interval_ms: number;
}

export interface BridgeEvent {
  kind: string;
  u0: number; u1: number; u2: number; u3: number; u4: number; u5: number;
  f0: number; f1: number; f2: number;
  b0: boolean; b1: boolean;
  text: string;
}

export const bridge = {
  abiVersion: () => invoke<number>("abi_version"),
  isInitialized: () => invoke<boolean>("is_initialized"),
  init: (dataDir: string) => invoke<boolean>("init", { dataDir }),
  configureMock: (frameDir: string, frameIntervalMs: number, loopFiles: boolean) =>
    invoke<CmdResult>("configure_mock", { frameDir, frameIntervalMs, loopFiles }),
  startCapture: () => invoke<CmdResult>("start_capture"),
  stopCapture: () => invoke<CmdResult>("stop_capture"),
  seekLatest: () => invoke<CmdResult>("seek_latest"),
  pollEvents: () => invoke<BridgeEvent[]>("poll_events"),
  fetchFrame: () => invoke<FrameMeta>("fetch_frame"),
  // Recording + review (bridge schema v2).
  startRecording: (filePath: string) => invoke<CmdResult>("start_recording", { filePath }),
  stopRecording: () => invoke<CmdResult>("stop_recording"),
  loadRecording: (filePath: string) => invoke<CmdResult>("load_recording", { filePath }),
  seekIndex: (frameIndex: number) => invoke<CmdResult>("seek_index", { frameIndex }),
  fetchFrameByIndex: (frameIndex: number) =>
    invoke<FrameMeta>("fetch_frame_by_index", { frameIndex }),
  // Processing (bridge schema v3).
  applyProcessing: (realtimeEnabled: boolean, pixelToMicron: number) =>
    invoke<CmdResult>("apply_processing", { realtimeEnabled, pixelToMicron }),
  fetchProcessingStats: () => invoke<ProcessingStats>("fetch_processing_stats"),
  // Operation state + bounded-queue observability (bridge schema v4, BE-1).
  cancelOperation: (operationId: number) =>
    invoke<CmdResult>("cancel_operation", { operationId }),
  queueOverflowTotal: () => invoke<number>("queue_overflow_total"),
  // Experiment lifecycle (bridge schema v5, BE-4) — the backend owns
  // preconditions, accumulation, flush, metadata ordering, and recovery.
  experimentStart: (outputPath: string) =>
    invoke<CmdResult>("experiment_start", { outputPath }),
  experimentStop: () => invoke<CmdResult>("experiment_stop"),
  experimentCancel: () => invoke<CmdResult>("experiment_cancel"),
  fetchExperimentStatus: () => invoke<ExperimentStatus>("fetch_experiment_status"),
  // Paged HDF5 review + export jobs (schema v9, BE-6).
  fetchReviewMetadata: () => invoke<ReviewMetadata>("fetch_review_metadata"),
  fetchReviewMetricsPage: (valid: boolean, offset: number, count: number) =>
    invoke<ReviewMetricsPage>("fetch_review_metrics_page", { valid, offset, count }),
  fetchReviewImage: (dataset: number, index: number) =>
    invoke<FrameMeta>("fetch_review_image", { dataset, index }),
  reviewImageBytes: async (): Promise<Uint8Array> => {
    const buf = await invoke<ArrayBuffer>("review_image_bytes");
    return new Uint8Array(buf);
  },
  reviewExportCsv: (outputPath: string) =>
    invoke<CmdResult>("review_export_csv", { outputPath }),
  // Processing config / ROI / background / core identity (schema v8, BE-3).
  fetchProcessingConfigJson: () =>
    invoke<{ valid: boolean; json: string }>("fetch_processing_config_json"),
  applyProcessingConfigJson: (json: string) =>
    invoke<CmdResult>("apply_processing_config_json", { json }),
  setProcessingRoi: (x: number, y: number, w: number, h: number) =>
    invoke<CmdResult>("set_processing_roi", { x, y, w, h }),
  fetchBackground: () => invoke<FrameMeta>("fetch_background"),
  backgroundBytes: async (): Promise<Uint8Array> => {
    const buf = await invoke<ArrayBuffer>("background_bytes");
    return new Uint8Array(buf);
  },
  setBackgroundFromCurrentFrame: () =>
    invoke<CmdResult>("set_background_from_current_frame"),
  clearBackgroundImage: () => invoke<CmdResult>("clear_background_image"),
  fetchProcessingCoreStatus: () =>
    invoke<ProcessingCoreStatus>("fetch_processing_core_status"),
  // Camera discovery/selection (bridge schema v7, BE-2).
  fetchCameraDiscovery: () => invoke<CameraDiscovery>("fetch_camera_discovery"),
  fetchCameraSelection: () => invoke<CameraSelection>("fetch_camera_selection"),
  selectHardwareCamera: (interfaceIndex: number, deviceIndex: number, label: string) =>
    invoke<CmdResult>("select_hardware_camera", { interfaceIndex, deviceIndex, label }),
  selectMindVisionCamera: (cameraIndex: number, label: string, configPath: string) =>
    invoke<CmdResult>("select_mindvision_camera", { cameraIndex, label, configPath }),
  applyCameraScript: (scriptPath: string) =>
    invoke<CmdResult>("apply_camera_script", { scriptPath }),
  resetHardwareCamera: () => invoke<CmdResult>("reset_hardware_camera"),
  // Monitoring + sorter trigger (bridge schema v6, BE-5). Monitoring is
  // visibility-gated: enable only while the Monitoring view is shown.
  monitoringSetActive: (active: boolean) =>
    invoke<CmdResult>("monitoring_set_active", { active }),
  monitoringClear: () => invoke<CmdResult>("monitoring_clear"),
  fetchMonitoringSnapshot: (maxRows: number) =>
    invoke<MonitoringSnapshot>("fetch_monitoring_snapshot", { maxRows }),
  triggerSetPulseDuration: (pulseUs: number) =>
    invoke<CmdResult>("trigger_set_pulse_duration", { pulseUs }),
  triggerManualPulse: () => invoke<CmdResult>("trigger_manual_pulse"),
  triggerPeriodicStart: (intervalMs: number) =>
    invoke<CmdResult>("trigger_periodic_start", { intervalMs }),
  triggerPeriodicStop: () => invoke<CmdResult>("trigger_periodic_stop"),
  fetchTriggerStatus: () => invoke<TriggerStatus>("fetch_trigger_status"),
  // Binary IPC response — raw Mono8 bytes, never base64 (ADR 0003).
  frameBytes: async (): Promise<Uint8Array> => {
    const buf = await invoke<ArrayBuffer>("frame_bytes");
    return new Uint8Array(buf);
  },
};

// Expand a Mono8 buffer (with row stride) into an RGBA ImageData for a canvas.
export function mono8ToImageData(
  bytes: Uint8Array,
  width: number,
  height: number,
  stride: number,
): ImageData {
  const rgba = new Uint8ClampedArray(width * height * 4);
  const rowStride = stride > 0 ? stride : width;
  for (let y = 0; y < height; y++) {
    const src = y * rowStride;
    for (let x = 0; x < width; x++) {
      const g = bytes[src + x] ?? 0;
      const d = (y * width + x) * 4;
      rgba[d] = g;
      rgba[d + 1] = g;
      rgba[d + 2] = g;
      rgba[d + 3] = 255;
    }
  }
  return new ImageData(rgba, width, height);
}
