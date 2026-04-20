import type { ReactNode } from "react";
import { Tabs, TabsList, TabsTrigger, TabsContent } from "../ui/tabs";

interface Tab {
  id: string;
  label: string;
}

interface TabBarProps {
  tabs: Tab[];
  activeTab: string;
  onTabChange: (id: string) => void;
  disabledTabs?: Set<string>;
  cornerWidget?: ReactNode;
  children: ReactNode;
}

export function TabBar({
  tabs,
  activeTab,
  onTabChange,
  disabledTabs,
  cornerWidget,
  children,
}: TabBarProps) {
  return (
    <Tabs value={activeTab} onValueChange={onTabChange} className="flex flex-col h-full">
      <div className="flex items-end border-b border-border bg-muted/30">
        <TabsList className="border-b-0">
          {tabs.map((tab) => (
            <TabsTrigger
              key={tab.id}
              value={tab.id}
              disabled={disabledTabs?.has(tab.id) ?? false}
            >
              {tab.label}
            </TabsTrigger>
          ))}
        </TabsList>
        {cornerWidget && (
          <div className="ml-auto flex items-center px-2 pb-1">
            {cornerWidget}
          </div>
        )}
      </div>
      {/* Render children directly - they handle content switching */}
      <div className="flex-1 min-h-0 overflow-hidden">{children}</div>
    </Tabs>
  );
}

export { TabsContent };
