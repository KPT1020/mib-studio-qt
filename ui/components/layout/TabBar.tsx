import type { ReactNode } from "react";

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
    <div className="flex flex-col h-full">
      {/* Qt-style tab bar row */}
      <div className="qt-tab-bar flex-shrink-0" style={{ alignItems: "end" }}>
        {/* Tab buttons using .qt-tab class */}
        {tabs.map((tab) => {
          const isDisabled = disabledTabs?.has(tab.id) ?? false;
          const isActive = activeTab === tab.id;

          return (
            <button
              key={tab.id}
              className="qt-tab"
              data-active={isActive}
              disabled={isDisabled}
              onClick={() => !isDisabled && onTabChange(tab.id)}
            >
              {tab.label}
            </button>
          );
        })}

        {/* Corner widget (right side, ml-auto) */}
        {cornerWidget && (
          <div className="ml-auto flex items-center px-2">
            {cornerWidget}
          </div>
        )}
      </div>

      {/* Content area takes remaining height */}
      <div className="flex-1 min-h-0 overflow-hidden">{children}</div>
    </div>
  );
}
