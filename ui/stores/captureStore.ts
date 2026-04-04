import { create } from "zustand";
import type { DiscoveredCamera, DiscoveredFramegrabber, MockCameraOptions } from "../types/backend";

interface CaptureState {
  isRunning: boolean;
  cameraConfigured: boolean;
  frameRate: number;
  dataRateMBps: number;
  framesProcessed: number;
  cameras: DiscoveredCamera[];
  framegrabbers: DiscoveredFramegrabber[];
  selectedCameraIndex: number | null;
  selectedFramegrabberIndex: number | null;
  mockOptions: MockCameraOptions | null;

  setRunning: (running: boolean) => void;
  setCameraConfigured: (configured: boolean) => void;
  setStats: (fps: number, dataRate: number, frames: number) => void;
  setCameras: (cameras: DiscoveredCamera[]) => void;
  setFramegrabbers: (fgs: DiscoveredFramegrabber[]) => void;
  setSelectedCamera: (index: number | null) => void;
  setSelectedFramegrabber: (index: number | null) => void;
  setMockOptions: (opts: MockCameraOptions | null) => void;
}

export const useCaptureStore = create<CaptureState>((set) => ({
  isRunning: false,
  cameraConfigured: false,
  frameRate: 0,
  dataRateMBps: 0,
  framesProcessed: 0,
  cameras: [],
  framegrabbers: [],
  selectedCameraIndex: null,
  selectedFramegrabberIndex: null,
  mockOptions: null,

  setRunning: (running) => set({ isRunning: running }),
  setCameraConfigured: (configured) => set({ cameraConfigured: configured }),
  setStats: (fps, dataRate, frames) =>
    set({ frameRate: fps, dataRateMBps: dataRate, framesProcessed: frames }),
  setCameras: (cameras) => set({ cameras }),
  setFramegrabbers: (fgs) => set({ framegrabbers: fgs }),
  setSelectedCamera: (index) => set({ selectedCameraIndex: index }),
  setSelectedFramegrabber: (index) => set({ selectedFramegrabberIndex: index }),
  setMockOptions: (opts) => set({ mockOptions: opts }),
}));
