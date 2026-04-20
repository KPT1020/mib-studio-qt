import { useRef, useEffect, useCallback, useState } from "react";
import { usePlaybackStore } from "../../stores/playbackStore";
import { useProcessingStore } from "../../stores/processingStore";
import type { Roi } from "../../types/backend";

/** ROI alignment constraints matching Qt implementation */
const ROI_OFFSET_X_STEP = 16;
const ROI_OFFSET_Y_STEP = 4;

interface ContourPoint {
  x: number;
  y: number;
}

interface ContourData {
  points: ContourPoint[];
  color: string;
}

interface DragState {
  startX: number;
  startY: number;
  currentX: number;
  currentY: number;
  dragging: boolean;
}

interface ImageCanvasProps {
  /** If true, show ROI overlay even when overlay mode is off (used by OverviewTab) */
  alwaysShowRoi?: boolean;
  /** External ROI override (for OverviewTab) */
  roi?: Roi | null;
  onRoiChange?: (roi: Roi | null) => void;
}

export function ImageCanvas({ alwaysShowRoi, roi: externalRoi, onRoiChange }: ImageCanvasProps = {}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const imageRef = useRef<HTMLImageElement | null>(null);
  const maskRef = useRef<HTMLImageElement | null>(null);
  const animFrameRef = useRef<number>(0);
  const contoursRef = useRef<ContourData[]>([]);

  const currentFrameBase64 = usePlaybackStore((s) => s.currentFrameBase64);
  const zoomMode = usePlaybackStore((s) => s.zoomMode);
  const overlayMode = useProcessingStore((s) => s.overlayMode);
  const maskBase64 = useProcessingStore((s) => s.maskBase64);
  const storeRoi = useProcessingStore((s) => s.roi);
  const setStoreRoi = useProcessingStore((s) => s.setRoi);

  const roi = externalRoi !== undefined ? externalRoi : storeRoi;
  const setRoi = onRoiChange ?? setStoreRoi;

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
    img.src = `data:image/png;base64,${currentFrameBase64}`;
  }, [currentFrameBase64]);

  // Load mask image when base64 changes
  useEffect(() => {
    if (!maskBase64) {
      maskRef.current = null;
      return;
    }
    const img = new Image();
    img.onload = () => {
      maskRef.current = img;
    };
    img.src = `data:image/png;base64,${maskBase64}`;
  }, [maskBase64]);

  // Calculate draw parameters
  const getDrawParams = useCallback(
    (canvas: HTMLCanvasElement, img: HTMLImageElement) => {
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
      return { drawX, drawY, drawW, drawH };
    },
    [zoomMode]
  );

  // Render loop - matching PlaybackPanel.cpp paintEvent order
  const render = useCallback(() => {
    const canvas = canvasRef.current;
    const container = containerRef.current;
    if (!canvas || !container) return;

    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    // Resize canvas to container with device pixel ratio for sharp rendering
    const rect = container.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    const w = Math.floor(rect.width);
    const h = Math.floor(rect.height);
    if (canvas.width !== w * dpr || canvas.height !== h * dpr) {
      canvas.width = w * dpr;
      canvas.height = h * dpr;
      canvas.style.width = `${w}px`;
      canvas.style.height = `${h}px`;
      ctx.scale(dpr, dpr);
    }

    // 1. Clear - black background (matching Qt)
    ctx.fillStyle = "#000000";
    ctx.fillRect(0, 0, w, h);

    const img = imageRef.current;
    if (!img) return;

    // Calculate scaled position
    const { drawX, drawY, drawW, drawH } = getDrawParams(
      { width: w, height: h } as HTMLCanvasElement,
      img
    );

    // 2. Draw frame image (scaled to fit or 100% zoom)
    ctx.drawImage(img, drawX, drawY, drawW, drawH);

    // 3. Draw overlay mask (semi-transparent) if mode includes mask
    const showMask = overlayMode === "mask" || overlayMode === "both";
    if (showMask && maskRef.current) {
      ctx.globalAlpha = 0.5;
      ctx.drawImage(maskRef.current, drawX, drawY, drawW, drawH);
      ctx.globalAlpha = 1.0;
    }

    // 4. Draw contours with per-contour color coding
    const showContours = overlayMode === "contours" || overlayMode === "both";
    if (showContours && contoursRef.current.length > 0) {
      const scaleX = drawW / img.width;
      const scaleY = drawH / img.height;

      for (const contour of contoursRef.current) {
        if (contour.points.length < 2) continue;
        ctx.strokeStyle = contour.color;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(
          drawX + contour.points[0].x * scaleX,
          drawY + contour.points[0].y * scaleY
        );
        for (let i = 1; i < contour.points.length; i++) {
          ctx.lineTo(
            drawX + contour.points[i].x * scaleX,
            drawY + contour.points[i].y * scaleY
          );
        }
        ctx.closePath();
        ctx.stroke();
      }
    }

    // 5. Draw ROI rectangle (yellow dashed) - matching Qt's cyan/yellow styling
    const shouldDrawRoi = alwaysShowRoi || overlayMode !== "off";
    if (shouldDrawRoi && roi) {
      const scaleX = drawW / img.width;
      const scaleY = drawH / img.height;
      ctx.strokeStyle = "#ffff00";
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

    // 6. Draw rubber-band selection (blue dash-dot) during drag
    if (drag.dragging) {
      const x = Math.min(drag.startX, drag.currentX);
      const y = Math.min(drag.startY, drag.currentY);
      const rw = Math.abs(drag.currentX - drag.startX);
      const rh = Math.abs(drag.currentY - drag.startY);
      ctx.strokeStyle = "#4488ff";
      ctx.setLineDash([8, 4, 2, 4]);
      ctx.lineWidth = 1;
      ctx.strokeRect(x, y, rw, rh);
      ctx.setLineDash([]);
    }
  }, [zoomMode, roi, drag, overlayMode, alwaysShowRoi, getDrawParams]);

  // Animation frame loop
  useEffect(() => {
    const loop = () => {
      render();
      animFrameRef.current = requestAnimationFrame(loop);
    };
    animFrameRef.current = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(animFrameRef.current);
  }, [render]);

  // Snap ROI coordinates to alignment constraints (matching Qt implementation)
  const snapRoi = (r: Roi, imgW: number, imgH: number): Roi => {
    let x = Math.round(r.x / ROI_OFFSET_X_STEP) * ROI_OFFSET_X_STEP;
    let y = Math.round(r.y / ROI_OFFSET_Y_STEP) * ROI_OFFSET_Y_STEP;
    let w = Math.round(r.w / ROI_OFFSET_X_STEP) * ROI_OFFSET_X_STEP;
    let h = Math.round(r.h / ROI_OFFSET_Y_STEP) * ROI_OFFSET_Y_STEP;
    x = Math.max(0, Math.min(x, imgW - w));
    y = Math.max(0, Math.min(y, imgH - h));
    w = Math.min(w, imgW - x);
    h = Math.min(h, imgH - y);
    if (w < ROI_OFFSET_X_STEP) w = ROI_OFFSET_X_STEP;
    if (h < ROI_OFFSET_Y_STEP) h = ROI_OFFSET_Y_STEP;
    return { x, y, w, h };
  };

  // Mouse handlers for ROI selection
  const handleMouseDown = (e: React.MouseEvent) => {
    if (e.button !== 0) return; // Left click only
    const rect = containerRef.current?.getBoundingClientRect();
    if (!rect) return;
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    setDrag({ startX: x, startY: y, currentX: x, currentY: y, dragging: true });
  };

  const handleMouseMove = (e: React.MouseEvent) => {
    if (!drag.dragging) return;
    const rect = containerRef.current?.getBoundingClientRect();
    if (!rect) return;
    setDrag((d) => ({
      ...d,
      currentX: e.clientX - rect.left,
      currentY: e.clientY - rect.top,
    }));
  };

  const handleMouseUp = () => {
    if (!drag.dragging) return;
    const img = imageRef.current;
    const container = containerRef.current;
    if (!img || !container) {
      setDrag((d) => ({ ...d, dragging: false }));
      return;
    }

    // Convert canvas coords to image coords
    const rect = container.getBoundingClientRect();
    const canvasW = rect.width;
    const canvasH = rect.height;
    const { drawX, drawY, drawW, drawH } = getDrawParams(
      { width: canvasW, height: canvasH } as HTMLCanvasElement,
      img
    );

    const scaleX = img.width / drawW;
    const scaleY = img.height / drawH;
    const x1 = Math.round((Math.min(drag.startX, drag.currentX) - drawX) * scaleX);
    const y1 = Math.round((Math.min(drag.startY, drag.currentY) - drawY) * scaleY);
    const x2 = Math.round((Math.max(drag.startX, drag.currentX) - drawX) * scaleX);
    const y2 = Math.round((Math.max(drag.startY, drag.currentY) - drawY) * scaleY);

    const w = x2 - x1;
    const h = y2 - y1;
    if (w > 5 && h > 5) {
      const rawRoi: Roi = {
        x: Math.max(0, x1),
        y: Math.max(0, y1),
        w: Math.min(w, img.width - Math.max(0, x1)),
        h: Math.min(h, img.height - Math.max(0, y1)),
      };
      const snapped = snapRoi(rawRoi, img.width, img.height);
      setRoi(snapped);
    }

    setDrag((d) => ({ ...d, dragging: false }));
  };

  // Context menu for Set Background / Clear ROI (matching Qt right-click menu)
  const handleContextMenu = (e: React.MouseEvent) => {
    e.preventDefault();
    // TODO: Show context menu with "Set Background" and "Clear ROI" options
  };

  return (
    <div
      ref={containerRef}
      className="w-full h-full relative"
      style={{ background: "#000" }}
    >
      <canvas
        ref={canvasRef}
        className="absolute inset-0"
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={handleMouseUp}
        onContextMenu={handleContextMenu}
      />
    </div>
  );
}
