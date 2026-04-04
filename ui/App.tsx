import { MainLayout } from "./components/layout/MainLayout";
import { useFrameStream } from "./hooks/useFrameStream";
import { useProcessingStats } from "./hooks/useProcessingStats";
import { ProcessingSettingsDialog } from "./components/dialogs/ProcessingSettingsDialog";
import { MonitoringSettingsDialog } from "./components/dialogs/MonitoringSettingsDialog";
import { ConversionFactorDialog } from "./components/dialogs/ConversionFactorDialog";
import { SyringePumpSettingsDialog } from "./components/dialogs/SyringePumpSettingsDialog";
import { MockConfigDialog } from "./components/dialogs/MockConfigDialog";
import { BufferSaveDialog } from "./components/dialogs/BufferSaveDialog";
import { FrameViewerDialog } from "./components/dialogs/FrameViewerDialog";
import { useAppStore } from "./stores/appStore";

export default function App() {
  // Global event listeners
  useFrameStream();
  useProcessingStats();

  const activeDialog = useAppStore((s) => s.activeDialog);
  const closeDialog = useAppStore((s) => s.closeDialog);

  return (
    <div className="flex flex-col h-screen w-screen overflow-hidden">
      {/* Menu bar is handled by Tauri native menu */}

      {/* Main content */}
      <div className="flex-1 min-h-0">
        <MainLayout />
      </div>

      {/* Status bar */}
      <StatusBar />

      {/* Dialogs */}
      {activeDialog === "processingSettings" && (
        <ProcessingSettingsDialog onClose={closeDialog} />
      )}
      {activeDialog === "monitoringSettings" && (
        <MonitoringSettingsDialog onClose={closeDialog} />
      )}
      {activeDialog === "conversionFactor" && (
        <ConversionFactorDialog onClose={closeDialog} />
      )}
      {activeDialog === "syringePumpSettings" && (
        <SyringePumpSettingsDialog onClose={closeDialog} />
      )}
      {activeDialog === "mockConfig" && (
        <MockConfigDialog onClose={closeDialog} />
      )}
      {activeDialog === "bufferSave" && (
        <BufferSaveDialog onClose={closeDialog} />
      )}
      {activeDialog === "frameViewer" && (
        <FrameViewerDialog onClose={closeDialog} />
      )}
    </div>
  );
}

function StatusBar() {
  const statusText = useAppStore((s) => s.statusText);

  return (
    <div
      className="flex items-center px-2 border-t border-neutral-300 bg-neutral-100 text-xs text-neutral-600"
      style={{ height: "var(--status-bar-height)" }}
    >
      <span>{statusText}</span>
    </div>
  );
}
