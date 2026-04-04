import type { ReactNode } from "react";

interface Tab {
  id: string;
  label: string;
}

interface TabBarProps {
  tabs: Tab[];
  activeTab: string;
  onTabChange: (id: string) => void;
  cornerWidget?: ReactNode;
  children: ReactNode;
}

export function TabBar({
  tabs,
  activeTab,
  onTabChange,
  cornerWidget,
  children,
}: TabBarProps) {
  return (
    <div className="flex flex-col h-full">
      {/* Tab header row */}
      <div className="flex items-center border-b border-neutral-300 bg-neutral-100 flex-shrink-0">
        {/* Tab buttons */}
        <div className="flex">
          {tabs.map((tab) => (
            <button
              key={tab.id}
              onClick={() => onTabChange(tab.id)}
              className={`px-4 py-1.5 text-xs border-r border-neutral-300 transition-colors ${
                activeTab === tab.id
                  ? "bg-white font-semibold border-b-2 border-b-blue-500"
                  : "hover:bg-neutral-200 cursor-pointer"
              }`}
            >
              {tab.label}
            </button>
          ))}
        </div>

        {/* Spacer */}
        <div className="flex-1" />

        {/* Corner widget */}
        {cornerWidget && (
          <div className="flex items-center gap-2 px-2">{cornerWidget}</div>
        )}
      </div>

      {/* Tab content */}
      <div className="flex-1 min-h-0 overflow-hidden">{children}</div>
    </div>
  );
}
