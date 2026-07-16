// GENERATED FILE — do not edit by hand.
// Source of truth: crates/mib-bridge/contract/bridge-contract.json
// Regenerate with: python3 scripts/gen_bridge_contract.py
// CI verifies this file with: python3 scripts/gen_bridge_contract.py --check

export const BRIDGE_ABI_VERSION = 10;

export const EVENT_KINDS = {
  FrameReady: 0,
  CameraStatus: 1,
  RecordingStatus: 2,
  ProcessingResult: 3,
  PlaybackPosition: 4,
  BackendError: 5,
  OperationStatus: 6,
  QueueOverflow: 7,
  ExperimentStatus: 8,
} as const;

export const COMMAND_TYPES = {
  Camera: 0,
  Recording: 1,
  ProcessingSettings: 2,
  RecordingLoad: 3,
  PlaybackSeek: 4,
  Operation: 5,
  Experiment: 6,
  Monitoring: 7,
  Trigger: 8,
  Review: 9,
  Pump: 10,
} as const;

export const CAMERA_TYPES = {
  EGrabber: 0,
  MindVision: 1,
  Mock: 2,
} as const;

export const CAMERA_SELECTION_MODES = {
  None: 0,
  Mock: 1,
  Hardware: 2,
  MindVision: 3,
} as const;

export const REVIEW_IMAGE_DATASETS = {
  ValidImage: 0,
  InvalidImage: 1,
  RecordedImage: 2,
  ValidMask: 3,
  InvalidMask: 4,
} as const;

export const PUMP_IDS = {
  Sample: 0,
  Sheath: 1,
} as const;

export const PUMP_RUN_STATES = {
  Stop: 0,
  Forward: 1,
  Backward: 2,
  Pause: 3,
} as const;

export const PUMP_DIRECTIONS = {
  Infuse: 0,
  Withdraw: 1,
} as const;

export const EXPERIMENT_STATES = {
  Idle: 0,
  Starting: 1,
  Active: 2,
  Stopping: 3,
  Failed: 4,
} as const;

export const ERROR_SOURCES = {
  Lifecycle: 0,
  Camera: 1,
  Recording: 2,
  Processing: 3,
  Playback: 4,
  Experiment: 5,
  Monitoring: 6,
  Hardware: 7,
  ConfigCore: 8,
  Review: 9,
  Export: 10,
  Platform: 11,
} as const;

export const OPERATION_KINDS = {
  RecordingLoad: 0,
  Experiment: 1,
  Export: 2,
  BatchMetrics: 3,
  MaskRegeneration: 4,
  Reanalysis: 5,
  PumpScan: 6,
} as const;

export const OPERATION_STATES = {
  Started: 0,
  Progress: 1,
  Completed: 2,
  Failed: 3,
  Cancelled: 4,
  TimedOut: 5,
} as const;

export const CAMERA_STATES = {
  Unconfigured: 0,
  Configured: 1,
  Starting: 2,
  Running: 3,
  Stopped: 4,
  Error: 5,
} as const;

export const RECORDING_STATES = {
  Idle: 0,
  Starting: 1,
  Recording: 2,
  Stopped: 3,
  Loaded: 4,
  Error: 5,
} as const;

export const EVENT_KIND_NAMES: Readonly<Record<number, string>> = {
  0: "FrameReady",
  1: "CameraStatus",
  2: "RecordingStatus",
  3: "ProcessingResult",
  4: "PlaybackPosition",
  5: "BackendError",
  6: "OperationStatus",
  7: "QueueOverflow",
  8: "ExperimentStatus",
};

export type BridgeEventKindName = keyof typeof EVENT_KINDS;
