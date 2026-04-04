import { useState } from "react";
import { TabBar } from "../layout/TabBar";

const CONFIG_TABS = [
  { id: "appConfig", label: "App Config" },
  { id: "cameraScript", label: "Camera Script" },
];

export function ConfigTabs() {
  const [activeTab, setActiveTab] = useState("appConfig");
  const [jsonContent, setJsonContent] = useState("{}");
  const [configPath, setConfigPath] = useState("");
  const [unsaved, setUnsaved] = useState(false);
  const [viewMode, setViewMode] = useState<"json" | "table">("json");
  const [profileName, setProfileName] = useState("Default");
  const [cameraScript, setCameraScript] = useState("");
  const [cameraScriptPath, setCameraScriptPath] = useState("");
  const [cameraUnsaved, setCameraUnsaved] = useState(false);

  return (
    <TabBar tabs={CONFIG_TABS} activeTab={activeTab} onTabChange={setActiveTab}>
      {activeTab === "appConfig" && (
        <div className="flex flex-col h-full p-1.5" style={{ gap: "var(--spacing-sm)" }}>
          {/* Top button row */}
          <div className="flex items-center gap-1.5 flex-shrink-0 flex-wrap">
            <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
              Reset
            </button>
            <button
              onClick={() => setUnsaved(false)}
              className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
            >
              Save
            </button>
            <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
              Browse...
            </button>
            <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
              Clear
            </button>
            <div className="flex-1" />
            <span className="text-xs text-neutral-500 truncate max-w-[400px]">
              {configPath || "No config loaded"}
            </span>
            <div style={{ width: "8px" }} />
            {unsaved && (
              <span className="text-xs" style={{ color: "var(--color-unsaved)" }}>
                Unsaved changes - click Save to apply.
              </span>
            )}
            <div style={{ width: "8px" }} />
            <span className="text-xs text-neutral-600">Profile:</span>
            <select
              value={profileName}
              onChange={(e) => setProfileName(e.target.value)}
              className="text-xs border border-neutral-400 rounded px-1 py-0.5"
            >
              <option value="Default">Default</option>
            </select>
            <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
              Save Profile
            </button>
            <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
              Rename
            </button>
            <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
              Delete
            </button>
            <button
              onClick={() => setViewMode(viewMode === "json" ? "table" : "json")}
              className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
            >
              {viewMode === "json" ? "Table View" : "JSON View"}
            </button>
          </div>

          {/* Content area */}
          <div className="flex-1 min-h-0">
            {viewMode === "json" ? (
              <textarea
                value={jsonContent}
                onChange={(e) => {
                  setJsonContent(e.target.value);
                  setUnsaved(true);
                }}
                className="w-full h-full font-mono text-xs p-2 border border-neutral-300 resize-none bg-white"
                style={{ whiteSpace: "pre", overflowWrap: "normal", overflowX: "auto" }}
                spellCheck={false}
              />
            ) : (
              <div className="w-full h-full overflow-auto border border-neutral-300 bg-white p-2">
                <p className="text-xs text-neutral-400">
                  Table view - grouped config parameters will render here
                </p>
              </div>
            )}
          </div>
        </div>
      )}

      {activeTab === "cameraScript" && (
        <div className="flex flex-col h-full p-1.5" style={{ gap: "var(--spacing-sm)" }}>
          {/* Button row */}
          <div className="flex items-center gap-1.5 flex-shrink-0">
            <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
              Reset
            </button>
            <button
              onClick={() => setCameraUnsaved(false)}
              className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
            >
              Save
            </button>
            <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
              Apply to Camera
            </button>
            <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
              Browse...
            </button>
            <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
              Clear
            </button>
            <div className="flex-1" />
            <span className="text-xs text-neutral-500 truncate max-w-[400px]">
              {cameraScriptPath || "No script loaded"}
            </span>
            <div style={{ width: "8px" }} />
            {cameraUnsaved && (
              <span className="text-xs" style={{ color: "var(--color-unsaved)" }}>
                Unsaved changes - click Save to apply.
              </span>
            )}
          </div>

          {/* Editor */}
          <div className="flex-1 min-h-0">
            <textarea
              value={cameraScript}
              onChange={(e) => {
                setCameraScript(e.target.value);
                setCameraUnsaved(true);
              }}
              className="w-full h-full font-mono text-xs p-2 border border-neutral-300 resize-none bg-white"
              style={{ whiteSpace: "pre", overflowWrap: "normal", overflowX: "auto" }}
              spellCheck={false}
            />
          </div>
        </div>
      )}
    </TabBar>
  );
}
