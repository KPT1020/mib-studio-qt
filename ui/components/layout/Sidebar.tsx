import { useAppStore } from "../../stores/appStore";
import { BackgroundPreview } from "../panels/BackgroundPreview";
import { StatisticsPanel } from "../panels/StatisticsPanel";
import { NanopositionerPanel } from "../panels/NanopositionerPanel";
import { SyringePumpPanel } from "../panels/SyringePumpPanel";

export function Sidebar() {
  const collapsed = useAppStore((s) => s.sidebarCollapsed);
  const toggleSidebar = useAppStore((s) => s.toggleSidebar);

  return (
    <div
      className="flex h-full"
      style={{
        width: collapsed
          ? "var(--sidebar-collapsed-width)"
          : "var(--sidebar-expanded-width)",
        minWidth: collapsed
          ? "var(--sidebar-collapsed-width)"
          : "var(--sidebar-expanded-width)",
        transition: "width 200ms ease, min-width 200ms ease",
        /* SizePolicy: Fixed horizontal, Expanding vertical */
        flexShrink: 0,
        flexGrow: 0,
      }}
    >
      {/* LEFT: QScrollArea (widgetResizable=true, frameShape=NoFrame) */}
      {!collapsed && (
        <div
          className="flex-1 overflow-y-auto overflow-x-hidden"
          style={{
            /* QHBoxLayout margin 0, spacing 0 */
            margin: 0,
            padding: 0,
          }}
        >
          <div className="flex flex-col">
            {/* BackgroundPreviewWidget */}
            <BackgroundPreview />

            {/* QFrame::HLine separator */}
            <hr className="qt-separator" />

            {/* StatisticsPanel */}
            <StatisticsPanel />

            {/* QFrame::HLine separator */}
            <hr className="qt-separator" />

            {/* NanopositionerTab */}
            <NanopositionerPanel />

            {/* QFrame::HLine separator */}
            <hr className="qt-separator" />

            {/* SyringePumpTab */}
            <SyringePumpPanel />

            {/* Stretch at bottom */}
            <div className="flex-1" />
          </div>
        </div>
      )}

      {/* RIGHT: Toggle button (30x30, fixed size, autoRaise style) */}
      <button
        onClick={toggleSidebar}
        className="qt-tool-btn flex items-center justify-center flex-shrink-0"
        style={{
          width: "var(--sidebar-collapsed-width)",
          height: "var(--sidebar-collapsed-width)",
          /* autoRaise: border only on hover (handled by qt-tool-btn) */
        }}
        title={collapsed ? "Expand sidebar" : "Collapse sidebar"}
      >
        {collapsed ? "\u25B6" : "\u25C0"}
      </button>
    </div>
  );
}
