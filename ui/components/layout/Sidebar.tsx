import { useAppStore } from "../../stores/appStore";
import { BackgroundPreview } from "../panels/BackgroundPreview";
import { StatisticsPanel } from "../panels/StatisticsPanel";
import { NanopositionerPanel } from "../panels/NanopositionerPanel";
import { SyringePumpPanel } from "../panels/SyringePumpPanel";
import { Separator } from "../ui/separator";
import { Button } from "../ui/button";
import { ChevronLeft, ChevronRight } from "lucide-react";

export function Sidebar() {
  const collapsed = useAppStore((s) => s.sidebarCollapsed);
  const toggleSidebar = useAppStore((s) => s.toggleSidebar);

  return (
    <div
      className="flex h-full border-r border-border bg-muted/20"
      style={{
        width: collapsed ? "var(--sidebar-collapsed-width)" : "var(--sidebar-expanded-width)",
        minWidth: collapsed ? "var(--sidebar-collapsed-width)" : "var(--sidebar-expanded-width)",
        transition: "width 200ms ease, min-width 200ms ease",
        flexShrink: 0,
        flexGrow: 0,
      }}
    >
      {!collapsed && (
        <div className="flex-1 overflow-y-auto overflow-x-hidden">
          <div className="flex flex-col">
            <BackgroundPreview />
            <Separator />
            <StatisticsPanel />
            <Separator />
            <NanopositionerPanel />
            <Separator />
            <SyringePumpPanel />
            <div className="flex-1" />
          </div>
        </div>
      )}

      <Button
        variant="ghost"
        size="icon"
        onClick={toggleSidebar}
        className="flex-shrink-0 rounded-none"
        style={{
          width: "var(--sidebar-collapsed-width)",
          height: "var(--sidebar-collapsed-width)",
        }}
        title={collapsed ? "Expand sidebar" : "Collapse sidebar"}
      >
        {collapsed ? <ChevronRight className="h-4 w-4" /> : <ChevronLeft className="h-4 w-4" />}
      </Button>
    </div>
  );
}
