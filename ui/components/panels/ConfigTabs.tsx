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
    <div style={{ display: "flex", flexDirection: "column", height: "100%" }}>
      <TabBar tabs={CONFIG_TABS} activeTab={activeTab} onTabChange={setActiveTab}>
        {activeTab === "appConfig" && (
          <div style={{ display: "flex", flexDirection: "column", height: "100%" }}>
            {/* Button row: QHBoxLayout */}
            <div
              style={{
                display: "flex",
                alignItems: "center",
                gap: 6,
                flexShrink: 0,
                flexWrap: "wrap",
                padding: "4px 6px",
              }}
            >
              <button className="qt-btn">Reset</button>
              <button className="qt-btn" onClick={() => setUnsaved(false)}>
                Save
              </button>
              <button className="qt-btn">Browse...</button>
              <button className="qt-btn">Clear</button>

              {/* stretch */}
              <div style={{ flex: 1 }} />

              {/* configPathLabel (max 400px) */}
              <span
                style={{
                  fontSize: 12,
                  color: "#888",
                  overflow: "hidden",
                  textOverflow: "ellipsis",
                  whiteSpace: "nowrap",
                  maxWidth: 400,
                }}
              >
                {configPath || "No config loaded"}
              </span>

              {/* 8px spacer */}
              <div style={{ width: 8 }} />

              {/* unsaved label (orange) */}
              {unsaved && (
                <span style={{ fontSize: 12, color: "orange" }}>
                  Unsaved changes - click Save to apply.
                </span>
              )}

              {/* 8px spacer */}
              <div style={{ width: 8 }} />

              {/* "Profile:" label */}
              <span style={{ fontSize: 12 }}>Profile:</span>

              {/* QComboBox (profiles) */}
              <select
                className="qt-select"
                value={profileName}
                onChange={(e) => setProfileName(e.target.value)}
              >
                <option value="Default">Default</option>
              </select>

              <button className="qt-btn">Save Profile</button>
              <button className="qt-btn">Rename</button>
              <button className="qt-btn">Delete</button>

              {/* JSON/Table toggle button */}
              <button
                className="qt-btn"
                onClick={() => setViewMode(viewMode === "json" ? "table" : "json")}
              >
                {viewMode === "json" ? "Table View" : "JSON View"}
              </button>
            </div>

            {/* QStackedWidget: index 0 = QPlainTextEdit (no wrap), index 1 = scroll area with grid */}
            <div style={{ flex: 1, minHeight: 0 }}>
              {viewMode === "json" ? (
                <textarea
                  className="qt-input"
                  value={jsonContent}
                  onChange={(e) => {
                    setJsonContent(e.target.value);
                    setUnsaved(true);
                  }}
                  style={{
                    width: "100%",
                    height: "100%",
                    fontFamily: "monospace",
                    resize: "none",
                    whiteSpace: "pre",
                    overflowWrap: "normal",
                    overflowX: "auto",
                    boxSizing: "border-box",
                  }}
                  spellCheck={false}
                />
              ) : (
                <div
                  style={{
                    width: "100%",
                    height: "100%",
                    overflow: "auto",
                    border: "1px solid #ababab",
                    background: "white",
                    padding: 8,
                    boxSizing: "border-box",
                  }}
                >
                  <p style={{ fontSize: 12, color: "#999" }}>
                    Table view - grouped config parameters will render here
                  </p>
                </div>
              )}
            </div>
          </div>
        )}

        {activeTab === "cameraScript" && (
          <div style={{ display: "flex", flexDirection: "column", height: "100%" }}>
            {/* Button row */}
            <div
              style={{
                display: "flex",
                alignItems: "center",
                gap: 6,
                flexShrink: 0,
                padding: "4px 6px",
              }}
            >
              <button className="qt-btn">Reset</button>
              <button className="qt-btn" onClick={() => setCameraUnsaved(false)}>
                Save
              </button>
              <button className="qt-btn">Apply to Camera</button>
              <button className="qt-btn">Browse...</button>
              <button className="qt-btn">Clear</button>

              {/* stretch */}
              <div style={{ flex: 1 }} />

              {/* path label */}
              <span
                style={{
                  fontSize: 12,
                  color: "#888",
                  overflow: "hidden",
                  textOverflow: "ellipsis",
                  whiteSpace: "nowrap",
                  maxWidth: 400,
                }}
              >
                {cameraScriptPath || "No script loaded"}
              </span>

              {/* 8px spacer */}
              <div style={{ width: 8 }} />

              {/* unsaved label */}
              {cameraUnsaved && (
                <span style={{ fontSize: 12, color: "orange" }}>
                  Unsaved changes - click Save to apply.
                </span>
              )}
            </div>

            {/* QPlainTextEdit (no wrap) */}
            <div style={{ flex: 1, minHeight: 0 }}>
              <textarea
                className="qt-input"
                value={cameraScript}
                onChange={(e) => {
                  setCameraScript(e.target.value);
                  setCameraUnsaved(true);
                }}
                style={{
                  width: "100%",
                  height: "100%",
                  fontFamily: "monospace",
                  resize: "none",
                  whiteSpace: "pre",
                  overflowWrap: "normal",
                  overflowX: "auto",
                  boxSizing: "border-box",
                }}
                spellCheck={false}
              />
            </div>
          </div>
        )}
      </TabBar>
    </div>
  );
}
