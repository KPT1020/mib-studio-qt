import { create } from "zustand";
import type { ProcessingConfig, ProcessedFrame, Roi, OverlayMode } from "../types/backend";

const defaultConfig: ProcessingConfig = {
  gaussian_blur_size: 5,
  bg_subtract_threshold: 20,
  morph_kernel_size: 3,
  morph_iterations: 1,
  area_threshold_min: 100,
  area_threshold_max: 10000,
  deformability_threshold_min: 0.0,
  deformability_threshold_max: 1.0,
  enable_border_check: true,
  enable_area_range_check: true,
  enable_deformability_range_check: true,
  area_ratio_threshold_max: 1.5,
  enable_area_ratio_check: false,
  require_single_inner_contour: false,
  empty_frame_pixel_threshold: 10,
  auto_background_enabled: false,
  auto_background_empty_frames: 5,
  auto_background_cooldown_frames: 100,
  enable_target_group: false,
  target_group_area_min: 200,
  target_group_area_max: 5000,
  target_group_deformability_min: 0.0,
  target_group_deformability_max: 0.5,
  enable_target_group_emodulus: false,
  target_group_emodulus_min: 0,
  target_group_emodulus_max: 100,
  multi_image_enabled: false,
  multi_image_count: 1,
};

interface ProcessingState {
  config: ProcessingConfig;
  roi: Roi | null;
  overlayMode: OverlayMode;
  validFrames: ProcessedFrame[];
  invalidFrames: ProcessedFrame[];
  monitoringValidFrames: ProcessedFrame[];
  monitoringInvalidFrames: ProcessedFrame[];
  algoFps: number;
  validFps: number;
  invalidFps: number;
  algoAvgUs: number;
  totalValidFlushed: number;
  backgroundImageBase64: string | null;

  setConfig: (config: ProcessingConfig) => void;
  setRoi: (roi: Roi | null) => void;
  setOverlayMode: (mode: OverlayMode) => void;
  setValidFrames: (frames: ProcessedFrame[]) => void;
  setInvalidFrames: (frames: ProcessedFrame[]) => void;
  setMonitoringValidFrames: (frames: ProcessedFrame[]) => void;
  setMonitoringInvalidFrames: (frames: ProcessedFrame[]) => void;
  setProcessingStats: (algoFps: number, validFps: number, invalidFps: number, algoAvgUs: number, totalValid: number) => void;
  setBackgroundImage: (base64: string | null) => void;
}

export const useProcessingStore = create<ProcessingState>((set) => ({
  config: defaultConfig,
  roi: null,
  overlayMode: "off",
  validFrames: [],
  invalidFrames: [],
  monitoringValidFrames: [],
  monitoringInvalidFrames: [],
  algoFps: 0,
  validFps: 0,
  invalidFps: 0,
  algoAvgUs: 0,
  totalValidFlushed: 0,
  backgroundImageBase64: null,

  setConfig: (config) => set({ config }),
  setRoi: (roi) => set({ roi }),
  setOverlayMode: (mode) => set({ overlayMode: mode }),
  setValidFrames: (frames) => set({ validFrames: frames }),
  setInvalidFrames: (frames) => set({ invalidFrames: frames }),
  setMonitoringValidFrames: (frames) => set({ monitoringValidFrames: frames }),
  setMonitoringInvalidFrames: (frames) => set({ monitoringInvalidFrames: frames }),
  setProcessingStats: (algoFps, validFps, invalidFps, algoAvgUs, totalValid) =>
    set({ algoFps, validFps, invalidFps, algoAvgUs, totalValidFlushed: totalValid }),
  setBackgroundImage: (base64) => set({ backgroundImageBase64: base64 }),
}));
