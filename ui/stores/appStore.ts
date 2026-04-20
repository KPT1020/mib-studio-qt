import { create } from "zustand";

export type ActiveTab = "connect" | "overview" | "experiment" | "review";
export type ActiveDialog =
  | null
  | "processingSettings"
  | "monitoringSettings"
  | "conversionFactor"
  | "syringePumpSettings"
  | "mockConfig"
  | "bufferSave"
  | "frameViewer";

interface AppState {
  activeTab: ActiveTab;
  sidebarCollapsed: boolean;
  sidebarWidth: number;
  statusText: string;
  activeDialog: ActiveDialog;
  frameViewerFrame: { imageBase64: string; index: number } | null;

  setActiveTab: (tab: ActiveTab) => void;
  toggleSidebar: () => void;
  setSidebarWidth: (width: number) => void;
  setStatusText: (text: string) => void;
  openDialog: (dialog: ActiveDialog) => void;
  closeDialog: () => void;
  openFrameViewer: (frame: { imageBase64: string; index: number }) => void;
}

export const useAppStore = create<AppState>((set) => ({
  activeTab: "connect",
  sidebarCollapsed: false,
  sidebarWidth: 300,
  statusText: "Idle",
  activeDialog: null,
  frameViewerFrame: null,

  setActiveTab: (tab) => set({ activeTab: tab }),
  toggleSidebar: () =>
    set((s) => ({ sidebarCollapsed: !s.sidebarCollapsed })),
  setSidebarWidth: (width) => set({ sidebarWidth: width }),
  setStatusText: (text) => set({ statusText: text }),
  openDialog: (dialog) => set({ activeDialog: dialog }),
  closeDialog: () => set({ activeDialog: null }),
  openFrameViewer: (frame) =>
    set({ activeDialog: "frameViewer", frameViewerFrame: frame }),
}));
