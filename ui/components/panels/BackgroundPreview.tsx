import { useProcessingStore } from "../../stores/processingStore";

export function BackgroundPreview() {
  const backgroundImage = useProcessingStore((s) => s.backgroundImageBase64);

  return (
    <div className="p-1.5" style={{ padding: "var(--spacing-sm)" }}>
      <h4 className="text-xs font-bold mb-1">Background</h4>
      <div
        className="w-full bg-black flex items-center justify-center border border-neutral-300"
        style={{ aspectRatio: "4/3" }}
      >
        {backgroundImage ? (
          <img
            src={`data:image/jpeg;base64,${backgroundImage}`}
            alt="Background"
            className="max-w-full max-h-full object-contain"
          />
        ) : (
          <span className="text-xs text-neutral-500">No background</span>
        )}
      </div>
    </div>
  );
}
