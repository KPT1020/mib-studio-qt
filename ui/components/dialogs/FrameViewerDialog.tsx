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
        className="bg-white rounded-lg shadow-xl max-w-[90vw] max-h-[90vh] flex flex-col"
        onClick={(e) => e.stopPropagation()}
      >
        <div className="flex items-center justify-between px-4 py-2 border-b border-neutral-300">
          <span className="font-semibold text-sm">Frame #{frame.index}</span>
          <button
            onClick={onClose}
            className="text-neutral-500 hover:text-neutral-800 text-lg leading-none"
          >
            &times;
          </button>
        </div>
        <div className="flex-1 min-h-0 overflow-auto p-2 bg-black flex items-center justify-center">
          <img
            src={`data:image/jpeg;base64,${frame.imageBase64}`}
            alt={`Frame ${frame.index}`}
            className="max-w-full max-h-full object-contain"
            style={{ imageRendering: "pixelated" }}
          />
        </div>
      </div>
    </div>
  );
}
