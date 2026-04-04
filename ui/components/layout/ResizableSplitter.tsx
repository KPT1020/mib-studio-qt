import { Allotment } from "allotment";
import "allotment/dist/style.css";
import type { ReactNode } from "react";

interface ResizableSplitterProps {
  direction: "horizontal" | "vertical";
  children: ReactNode[];
  defaultSizes?: number[];
  minSizes?: number[];
  handleSize?: number;
  className?: string;
}

export function ResizableSplitter({
  direction,
  children,
  defaultSizes,
  minSizes,
  handleSize = 10,
  className,
}: ResizableSplitterProps) {
  return (
    <div className={`h-full w-full ${className ?? ""}`}>
      <Allotment
        vertical={direction === "vertical"}
        defaultSizes={defaultSizes}
        separator={handleSize}
      >
        {children.map((child, i) => (
          <Allotment.Pane
            key={i}
            minSize={minSizes?.[i] ?? 50}
            snap={false}
          >
            {child}
          </Allotment.Pane>
        ))}
      </Allotment>
    </div>
  );
}
