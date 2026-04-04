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
      className="flex h-full border-r border-neutral-300 bg-neutral-50 transition-all duration-200"
      style={{
        width: collapsed
          ? "var(--sidebar-collapsed-width)"
          : "var(--sidebar-expanded-width)",
        minWidth: collapsed
          ? "var(--sidebar-collapsed-width)"
          : "var(--sidebar-expanded-width)",
      }}
    >
      {/* Scrollable content */}
      {!collapsed && (
        <div className="flex-1 overflow-y-auto overflow-x-hidden">
          {/* Background preview */}
          <BackgroundPreview />

          <hr className="border-neutral-300" />

          {/* Statistics */}
          <StatisticsPanel />

          <hr className="border-neutral-300" />

          {/* Nanopositioner */}
          <NanopositionerPanel />

          <hr className="border-neutral-300" />

          {/* Syringe Pump */}
          <SyringePumpPanel />
        </div>
      )}

      {/* Toggle button - always visible */}
      <button
        onClick={toggleSidebar}
        className="flex items-center justify-center bg-neutral-200 hover:bg-neutral-300 border-none cursor-pointer flex-shrink-0"
        style={{
          width: "var(--sidebar-collapsed-width)",
          height: "var(--sidebar-collapsed-width)",
        }}
        title={collapsed ? "Expand sidebar" : "Collapse sidebar"}
      >
        {collapsed ? "\u25B6" : "\u25C0"}
      </button>
    </div>
  );
}
