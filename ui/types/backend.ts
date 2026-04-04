// ---- Camera & Capture ----

export interface DiscoveredCamera {
  interfaceIndex: number;
  deviceIndex: number;
  interfaceID: string;
  deviceID: string;
  modelName: string;
  firmwareVersion: string;
  label: string;
}

export interface DiscoveredFramegrabber {
  interfaceIndex: number;
  deviceIndex: number;
  streamIndex: number;
  interfaceID: string;
  deviceID: string;
  streamID: string;
  modelName: string;
  label: string;
}

export interface CaptureStats {
  framesProcessed: number;
  lastFrameRate: number;
  lastDataRateMBps: number;
}

export interface MockCameraOptions {
  directory: string;
  intervalMs: number;
  loop: boolean;
}

// ---- Processing ----

export interface ProcessingConfig {
  gaussian_blur_size: number;
  bg_subtract_threshold: number;
  morph_kernel_size: number;
  morph_iterations: number;
  area_threshold_min: number;
  area_threshold_max: number;
  deformability_threshold_min: number;
  deformability_threshold_max: number;
  enable_border_check: boolean;
  enable_area_range_check: boolean;
  enable_deformability_range_check: boolean;
  area_ratio_threshold_max: number;
  enable_area_ratio_check: boolean;
  require_single_inner_contour: boolean;
  empty_frame_pixel_threshold: number;
  auto_background_enabled: boolean;
  auto_background_empty_frames: number;
  auto_background_cooldown_frames: number;
  enable_target_group: boolean;
  target_group_area_min: number;
  target_group_area_max: number;
  target_group_deformability_min: number;
  target_group_deformability_max: number;
  enable_target_group_emodulus: boolean;
  target_group_emodulus_min: number;
  target_group_emodulus_max: number;
  multi_image_enabled: boolean;
  multi_image_count: number;
}

export interface BrightnessQuantiles {
  q1: number;
  q2: number;
  q3: number;
  q4: number;
}

export interface FilterResult {
  isValid: boolean;
  touchesBorder: boolean;
  hasSingleInnerContour: boolean;
  inRange: boolean;
  innerContourCount: number;
  deformability: number;
  area: number;
  areaRatio: number;
  ringRatio: number;
  youngsModulus: number;
  brightness: BrightnessQuantiles;
  isTargetGroup: boolean;
}

export interface ProcessedFrame {
  index: number;
  timestampNs: number;
  imageBase64: string;
  imageWidth: number;
  imageHeight: number;
  validation: FilterResult;
}

export interface Roi {
  x: number;
  y: number;
  w: number;
  h: number;
}

// ---- Playback ----

export interface PlaybackRange {
  earliest: number;
  latest: number;
  count: number;
}

export interface FrameData {
  index: number;
  width: number;
  height: number;
  imageBase64: string;
  timestampNs: number;
}

// ---- Autofocus ----

export interface AutofocusConfig {
  focusSetpoint: number;
  focusRange: number;
  voltageStep: number;
  fineVoltageStep: number;
  maxVoltage: number;
  minVoltage: number;
  initialVoltage: number;
  manualVoltageStep: number;
  ringRatioStaleMs: number;
  requireNewSamplePerStep: boolean;
  minSamplesPerStep: number;
  safeShutdownVoltage: number;
  focusDirection: boolean;
}

export interface AutofocusStatus {
  connected: boolean;
  enabled: boolean;
  voltage: number;
  averageRingRatio: number;
  medianRingRatio: number;
  statusMessage: string;
}

// ---- Syringe Pump ----

export type PumpId = "sample" | "sheath";

export type RunStatus = "stop" | "forward" | "backward" | "pause";

export type PumpDirection = "infuse" | "withdraw";

export interface PumpConfig {
  comPort: number;
  baudRate: number;
  modbusAddress: number;
  flowRate: number;
  flowRateUnit: number;
  direction: PumpDirection;
}

export interface PumpStatus {
  connected: boolean;
  runStatus: RunStatus;
  currentFlowRate: number;
  accumulatedVolume: number;
  minFlowRate: number;
  maxFlowRate: number;
  stalled: boolean;
}

// ---- Config ----

export interface AppConfig {
  [key: string]: unknown;
}

// ---- Overlay ----

export type OverlayMode = "off" | "mask" | "contours" | "both";

export type HdfOverlayMode =
  | "none"
  | "all_contours"
  | "outer_inner_color"
  | "all_mask"
  | "filtered_mask";

export type ZoomMode = "fit" | "100%";

// ---- Monitoring Settings ----

export interface MonitoringSettings {
  updateIntervalMs: number;
  maxThumbnails: number;
}

// ---- Conversion Factor ----

export interface ConversionFactor {
  pixelToMicron: number;
}
