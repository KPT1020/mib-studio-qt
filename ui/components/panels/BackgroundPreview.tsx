import { useProcessingStore } from "../../stores/processingStore";

export function BackgroundPreview() {
  const backgroundImage = useProcessingStore((s) => s.backgroundImageBase64);

  return (
    <div className="p-3">
      <span className="font-semibold text-sm block mb-2">Background</span>
      <div
        className="w-full bg-black flex items-center justify-center rounded-md border border-border overflow-hidden"
        style={{ aspectRatio: "4/3" }}
      >
        {backgroundImage ? (
          <img
            src={`data:image/jpeg;base64,${backgroundImage}`}
            alt="Background"
            className="max-w-full max-h-full object-contain"
          />
        ) : (
          <span className="text-xs text-muted-foreground">No background</span>
        )}
      </div>
    </div>
  );
}
