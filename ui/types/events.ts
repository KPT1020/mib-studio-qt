export interface FrameNewEvent {
  index: number;
  width: number;
  height: number;
  imageBase64: string;
  timestampNs: number;
}

export interface StatsUpdateEvent {
  captureFrameRate: number;
  captureDataRateMbps: number;
  algoFps: number;
  validFps: number;
  invalidFps: number;
  algoAvgUs: number;
  totalValidFlushed: number;
}

export interface BackgroundCapturedEvent {
  imageBase64: string;
  frameIndex: number;
}

export interface AutofocusStatusEvent {
  connected: boolean;
  enabled: boolean;
  voltage: number;
  averageRingRatio: number;
  medianRingRatio: number;
  statusMessage: string;
}

export interface PumpStatusEvent {
  pumpId: "sample" | "sheath";
  connected: boolean;
  runStatus: string;
  currentFlowRate: number;
  accumulatedVolume: number;
  stalled: boolean;
}
