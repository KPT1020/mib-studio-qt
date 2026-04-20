import { useState } from "react";
import { Tabs, TabsList, TabsTrigger, TabsContent } from "../ui/tabs";
import { Button } from "../ui/button";
import { Textarea } from "../ui/textarea";

export function ConfigTabs() {
  const [jsonContent, setJsonContent] = useState("{}");
  const [configPath] = useState("");
  const [unsaved, setUnsaved] = useState(false);
  const [viewMode, setViewMode] = useState<"json" | "table">("json");
  const [profileName, setProfileName] = useState("Default");
  const [cameraScript, setCameraScript] = useState("");
  const [cameraScriptPath] = useState("");
  const [cameraUnsaved, setCameraUnsaved] = useState(false);

  return (
    <Tabs defaultValue="appConfig" className="flex flex-col h-full">
      <div className="flex items-end border-b border-border bg-muted/30">
        <TabsList className="border-b-0">
          <TabsTrigger value="appConfig">App Config</TabsTrigger>
          <TabsTrigger value="cameraScript">Camera Script</TabsTrigger>
        </TabsList>
      </div>

      <TabsContent value="appConfig" className="overflow-hidden">
        <div className="flex flex-col h-full">
          {/* Button row */}
          <div className="flex items-center gap-2 flex-shrink-0 flex-wrap px-2 py-1">
            <Button size="sm" variant="outline">Reset</Button>
            <Button size="sm" onClick={() => setUnsaved(false)}>Save</Button>
            <Button size="sm" variant="outline">Browse...</Button>
            <Button size="sm" variant="outline">Clear</Button>
            <div className="flex-1" />
            <span className="text-xs text-muted-foreground truncate max-w-[400px]">
              {configPath || "No config loaded"}
            </span>
            {unsaved && (
              <span className="text-xs text-amber-500">Unsaved changes - click Save to apply.</span>
            )}
            <span className="text-xs text-muted-foreground">Profile:</span>
            <select
              className="select-styled"
              value={profileName}
              onChange={(e) => setProfileName(e.target.value)}
              style={{ width: 120 }}
            >
              <option value="Default">Default</option>
            </select>
            <Button size="sm" variant="outline">Save Profile</Button>
            <Button size="sm" variant="outline">Rename</Button>
            <Button size="sm" variant="outline">Delete</Button>
            <Button
              size="sm"
              variant="secondary"
              onClick={() => setViewMode(viewMode === "json" ? "table" : "json")}
            >
              {viewMode === "json" ? "Table View" : "JSON View"}
            </Button>
          </div>

          {/* Content area */}
          <div className="flex-1 min-h-0 px-2 pb-2">
            {viewMode === "json" ? (
              <Textarea
                value={jsonContent}
                onChange={(e) => {
                  setJsonContent(e.target.value);
                  setUnsaved(true);
                }}
                className="h-full resize-none font-mono text-xs"
                style={{ whiteSpace: "pre", overflowWrap: "normal", overflowX: "auto" }}
                spellCheck={false}
              />
            ) : (
              <div className="h-full overflow-auto rounded-md border border-border bg-background p-3">
                <p className="text-xs text-muted-foreground">
                  Table view - grouped config parameters will render here
                </p>
              </div>
            )}
          </div>
        </div>
      </TabsContent>

      <TabsContent value="cameraScript" className="overflow-hidden">
        <div className="flex flex-col h-full">
          {/* Button row */}
          <div className="flex items-center gap-2 flex-shrink-0 px-2 py-1">
            <Button size="sm" variant="outline">Reset</Button>
            <Button size="sm" onClick={() => setCameraUnsaved(false)}>Save</Button>
            <Button size="sm" variant="outline">Apply to Camera</Button>
            <Button size="sm" variant="outline">Browse...</Button>
            <Button size="sm" variant="outline">Clear</Button>
            <div className="flex-1" />
            <span className="text-xs text-muted-foreground truncate max-w-[400px]">
              {cameraScriptPath || "No script loaded"}
            </span>
            {cameraUnsaved && (
              <span className="text-xs text-amber-500">Unsaved changes - click Save to apply.</span>
            )}
          </div>

          {/* Editor */}
          <div className="flex-1 min-h-0 px-2 pb-2">
            <Textarea
              value={cameraScript}
              onChange={(e) => {
                setCameraScript(e.target.value);
                setCameraUnsaved(true);
              }}
              className="h-full resize-none font-mono text-xs"
              style={{ whiteSpace: "pre", overflowWrap: "normal", overflowX: "auto" }}
              spellCheck={false}
            />
          </div>
        </div>
      </TabsContent>
    </Tabs>
  );
}
