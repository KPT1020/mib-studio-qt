import { useProcessingStore } from "../../stores/processingStore";

export function BackgroundPreview() {
  const backgroundImage = useProcessingStore((s) => s.backgroundImageBase64);

  return (
    <div style={{ padding: "var(--spacing-sm)" }}>
      <span
        style={{
          fontWeight: "bold",
          fontSize: 12,
          display: "block",
          marginBottom: 4,
        }}
      >
        Background
      </span>
      <div
        style={{
          width: "100%",
          aspectRatio: "4/3",
          backgroundColor: "black",
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
          border: "1px solid #c0c0c0",
        }}
      >
        {backgroundImage ? (
          <img
            src={`data:image/jpeg;base64,${backgroundImage}`}
            alt="Background"
            style={{
              maxWidth: "100%",
              maxHeight: "100%",
              objectFit: "contain",
            }}
          />
        ) : (
          <span style={{ fontSize: 12, color: "#888" }}>No background</span>
        )}
      </div>
    </div>
  );
}
