import { create } from "zustand";

interface ExperimentState {
  isActive: boolean;
  hdf5FilePath: string | null;
  isRecording: boolean;
  recordingFrameCount: number;
  recordingFilteredCount: number;

  setActive: (active: boolean) => void;
  setHdf5FilePath: (path: string | null) => void;
  setRecording: (recording: boolean) => void;
  setRecordingCounts: (total: number, filtered: number) => void;
}

export const useExperimentStore = create<ExperimentState>((set) => ({
  isActive: false,
  hdf5FilePath: null,
  isRecording: false,
  recordingFrameCount: 0,
  recordingFilteredCount: 0,

  setActive: (active) => set({ isActive: active }),
  setHdf5FilePath: (path) => set({ hdf5FilePath: path }),
  setRecording: (recording) => set({ isRecording: recording }),
  setRecordingCounts: (total, filtered) =>
    set({ recordingFrameCount: total, recordingFilteredCount: filtered }),
}));
