import { useAppStore } from "../../stores/appStore";

interface Props {
  onClose: () => void;
}

export function FrameViewerDialog({ onClose }: Props) {
  const frame = useAppStore((s) => s.frameViewerFrame);

  if (!frame) return null;

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60"
      onClick={onClose}
    >
      <div
        className="bg-[var(--bg-window)] shadow-xl flex flex-col"
        style={{ maxWidth: "90vw", maxHeight: "90vh" }}
        onClick={(e) => e.stopPropagation()}
      >
        {/* Title bar matching Qt dialog */}
        <div className="flex items-center justify-between px-3 py-2 border-b border-[var(--border-widget)]">
          <span className="font-semibold text-sm">Frame #{frame.index}</span>
          <button
            onClick={onClose}
            className="qt-tool-btn text-lg leading-none"
          >
            &times;
          </button>
        </div>
        {/* Image area - black background, centered */}
        <div className="flex-1 min-h-0 overflow-auto p-1 bg-black flex items-center justify-center">
          <img
            src={`data:image/jpeg;base64,${frame.imageBase64}`}
            alt={`Frame ${frame.index}`}
            className="max-w-full max-h-full object-contain"
            style={{ imageRendering: "pixelated" }}
          />
        </div>
        <div className="flex justify-end px-3 py-2 border-t border-[var(--border-widget)]">
          <button onClick={onClose} className="qt-btn">Close</button>
        </div>
      </div>
    </div>
  );
}
