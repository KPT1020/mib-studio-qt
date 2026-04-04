import { useRef, useEffect, useCallback, useState } from "react";
import { usePlaybackStore } from "../../stores/playbackStore";
import { useProcessingStore } from "../../stores/processingStore";
import type { Roi } from "../../types/backend";

interface DragState {
  startX: number;
  startY: number;
  currentX: number;
  currentY: number;
  dragging: boolean;
}

export function ImageCanvas() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const imageRef = useRef<HTMLImageElement | null>(null);
  const animFrameRef = useRef<number>(0);

  const currentFrameBase64 = usePlaybackStore((s) => s.currentFrameBase64);
  const zoomMode = usePlaybackStore((s) => s.zoomMode);
  const overlayMode = useProcessingStore((s) => s.overlayMode);
  const roi = useProcessingStore((s) => s.roi);
  const setRoi = useProcessingStore((s) => s.setRoi);

  const [drag, setDrag] = useState<DragState>({
    startX: 0,
    startY: 0,
    currentX: 0,
    currentY: 0,
    dragging: false,
  });

  // Load frame image when base64 changes
  useEffect(() => {
    if (!currentFrameBase64) {
      imageRef.current = null;
      return;
    }
    const img = new Image();
    img.onload = () => {
      imageRef.current = img;
    };
    img.src = `data:image/jpeg;base64,${currentFrameBase64}`;
  }, [currentFrameBase64]);

  // Render loop
  const render = useCallback(() => {
    const canvas = canvasRef.current;
    const container = containerRef.current;
    if (!canvas || !container) return;

    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    // Resize canvas to container
    const rect = container.getBoundingClientRect();
    if (canvas.width !== rect.width || canvas.height !== rect.height) {
      canvas.width = rect.width;
      canvas.height = rect.height;
    }

    // Clear - black background
    ctx.fillStyle = "#000000";
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    const img = imageRef.current;
    if (!img) return;

    // Calculate scaled position
    let drawX = 0;
    let drawY = 0;
    let drawW = img.width;
    let drawH = img.height;

    if (zoomMode === "fit") {
      const scaleX = canvas.width / img.width;
      const scaleY = canvas.height / img.height;
      const scale = Math.min(scaleX, scaleY);
      drawW = img.width * scale;
      drawH = img.height * scale;
      drawX = (canvas.width - drawW) / 2;
      drawY = (canvas.height - drawH) / 2;
    } else {
      // 100% zoom - center
      drawX = (canvas.width - drawW) / 2;
      drawY = (canvas.height - drawH) / 2;
    }

    // Draw frame
    ctx.drawImage(img, drawX, drawY, drawW, drawH);

    // Draw ROI rectangle (yellow dashed)
    if (roi) {
      const scaleX = drawW / img.width;
      const scaleY = drawH / img.height;
      ctx.strokeStyle = "var(--color-roi, #ffff00)";
      ctx.setLineDash([6, 4]);
      ctx.lineWidth = 2;
      ctx.strokeRect(
        drawX + roi.x * scaleX,
        drawY + roi.y * scaleY,
        roi.w * scaleX,
        roi.h * scaleY
      );
      ctx.setLineDash([]);
    }

    // Draw rubber-band selection (blue dash-dot)
    if (drag.dragging) {
      const x = Math.min(drag.startX, drag.currentX);
      const y = Math.min(drag.startY, drag.currentY);
      const w = Math.abs(drag.currentX - drag.startX);
      const h = Math.abs(drag.currentY - drag.startY);
      ctx.strokeStyle = "#4488ff";
      ctx.setLineDash([8, 4, 2, 4]);
      ctx.lineWidth = 1;
      ctx.strokeRect(x, y, w, h);
      ctx.setLineDash([]);
    }
  }, [zoomMode, roi, drag, overlayMode]);

  // Animation frame loop
  useEffect(() => {
    const loop = () => {
      render();
      animFrameRef.current = requestAnimationFrame(loop);
    };
    animFrameRef.current = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(animFrameRef.current);
  }, [render]);

  // Mouse handlers for ROI selection
  const handleMouseDown = (e: React.MouseEvent) => {
    if (e.button !== 0) return; // Left click only
    const rect = canvasRef.current?.getBoundingClientRect();
    if (!rect) return;
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    setDrag({ startX: x, startY: y, currentX: x, currentY: y, dragging: true });
  };

  const handleMouseMove = (e: React.MouseEvent) => {
    if (!drag.dragging) return;
    const rect = canvasRef.current?.getBoundingClientRect();
    if (!rect) return;
    setDrag((d) => ({
      ...d,
      currentX: e.clientX - rect.left,
      currentY: e.clientY - rect.top,
    }));
  };

  const handleMouseUp = () => {
    if (!drag.dragging) return;
    const canvas = canvasRef.current;
    const img = imageRef.current;
    if (!canvas || !img) {
      setDrag((d) => ({ ...d, dragging: false }));
      return;
    }

    // Convert canvas coords to image coords
    let drawW: number, drawH: number, drawX: number, drawY: number;
    if (zoomMode === "fit") {
      const scaleX = canvas.width / img.width;
      const scaleY = canvas.height / img.height;
      const scale = Math.min(scaleX, scaleY);
      drawW = img.width * scale;
      drawH = img.height * scale;
      drawX = (canvas.width - drawW) / 2;
      drawY = (canvas.height - drawH) / 2;
    } else {
      drawW = img.width;
      drawH = img.height;
      drawX = (canvas.width - drawW) / 2;
      drawY = (canvas.height - drawH) / 2;
    }

    const scaleX = img.width / drawW;
    const scaleY = img.height / drawH;
    const x1 = Math.round((Math.min(drag.startX, drag.currentX) - drawX) * scaleX);
    const y1 = Math.round((Math.min(drag.startY, drag.currentY) - drawY) * scaleY);
    const x2 = Math.round((Math.max(drag.startX, drag.currentX) - drawX) * scaleX);
    const y2 = Math.round((Math.max(drag.startY, drag.currentY) - drawY) * scaleY);

    const w = x2 - x1;
    const h = y2 - y1;
    if (w > 5 && h > 5) {
      const newRoi: Roi = {
        x: Math.max(0, x1),
        y: Math.max(0, y1),
        w: Math.min(w, img.width - Math.max(0, x1)),
        h: Math.min(h, img.height - Math.max(0, y1)),
      };
      setRoi(newRoi);
    }

    setDrag((d) => ({ ...d, dragging: false }));
  };

  return (
    <div ref={containerRef} className="w-full h-full relative">
      <canvas
        ref={canvasRef}
        className="absolute inset-0 w-full h-full"
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={handleMouseUp}
      />
    </div>
  );
}
