import type { ProcessedFrame } from "../../types/backend";
import { useAppStore } from "../../stores/appStore";

interface FrameGridProps {
  frames: ProcessedFrame[];
  showOverlay?: boolean;
  selectedIndex?: number | null;
  onSelect?: (index: number) => void;
}

export function FrameGrid({
  frames,
  showOverlay = false,
  selectedIndex = null,
  onSelect,
}: FrameGridProps) {
  const openFrameViewer = useAppStore((s) => s.openFrameViewer);

  return (
    <div
      className="grid gap-1 p-1"
      style={{
        gridTemplateColumns: `repeat(var(--grid-columns, 5), var(--thumbnail-size))`,
      }}
    >
      {frames.map((frame, i) => (
        <div
          key={frame.index}
          className="cursor-pointer"
          onClick={() => onSelect?.(i)}
          onDoubleClick={() =>
            openFrameViewer({
              imageBase64: frame.imageBase64,
              index: frame.index,
            })
          }
          style={{
            width: "var(--thumbnail-size)",
            height: "var(--thumbnail-size)",
            border:
              selectedIndex === i
                ? "3px solid blue"
                : "2px solid gray",
            backgroundColor: selectedIndex === i ? "lightblue" : "transparent",
          }}
        >
          {frame.imageBase64 ? (
            <img
              src={`data:image/jpeg;base64,${frame.imageBase64}`}
              alt={`Frame ${frame.index}`}
              className="w-full h-full object-contain"
            />
          ) : (
            <div className="w-full h-full bg-neutral-200 flex items-center justify-center text-xs text-neutral-400">
              {frame.index}
            </div>
          )}
        </div>
      ))}
      {frames.length === 0 && (
        <p className="col-span-5 text-center text-xs text-neutral-400 py-4">
          No frames
        </p>
      )}
    </div>
  );
}
