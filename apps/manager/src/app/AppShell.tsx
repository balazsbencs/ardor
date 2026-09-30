import { Upload } from "lucide-react";
import { Suspense, lazy, useEffect, useState } from "react";

import { AssetsView } from "../assets/AssetsView";
import { ConnectionDialog } from "../connection/ConnectionDialog";
import { EditorProvider, usePresetEditorContext } from "../presets/editor/EditorContext";
import { StageWorkspace } from "../stage/StageWorkspace";
import { normalizePalette, paletteVariables, type PaletteId } from "../theme/accent";
import { SurfaceProvider } from "../theme/surface";
import { isHostedCloudRuntime } from "../runtime/platform";
import { AppBar } from "./AppBar";
import { useAppView } from "./useAppView";
import { useFileDrop } from "./useFileDrop";
import "./app.css";

const SettingsDialog = lazy(() => import("../settings/SettingsDialog").then((module) => ({ default: module.SettingsDialog })));

type ShellProps = { tone3000DeviceId?: string; onCloudDevices?: () => void; onConnection(): void; onSettings(): void };

/** The app bar and the active view. It sits inside EditorProvider so both views share one draft. */
function Shell({ tone3000DeviceId, onCloudDevices, onConnection, onSettings }: ShellProps) {
  const editor = usePresetEditorContext();
  const { view, assetKind, goto } = useAppView();
  const [pendingFiles, setPendingFiles] = useState<File[]>();
  // Files dropped anywhere open the Assets view, which queues them once.
  const dragging = useFileDrop((files) => { setPendingFiles(files); goto("assets", view === "assets" ? assetKind : undefined); });

  return (
    <>
      <AppBar view={view} onView={(next) => goto(next)} onConnection={onConnection} onSettings={onSettings} onCloudDevices={onCloudDevices} />
      {view === "edit"
        ? <StageWorkspace onManageFiles={(kind) => goto("assets", kind)} onConnection={onConnection} />
        : <AssetsView initialKind={assetKind} tone3000DeviceId={tone3000DeviceId} pendingFiles={pendingFiles}
            onFilesTaken={() => setPendingFiles(undefined)} onBack={() => goto("edit")}
            onOpenPreset={(location) => { editor.selectLocation(location); goto("edit"); }} />}
      {dragging && (
        <div className="adrop" role="presentation">
          <div><Upload aria-hidden="true" /><b>Drop to upload</b>
            <span>.nam files go to NAM models. .wav files go to {assetKind === "reverb-irs" && view === "assets" ? "Reverb IRs" : "Cabinet IRs"}.</span></div>
        </div>
      )}
    </>
  );
}

export function AppShell({ onCloudDevices, tone3000DeviceId }: { onCloudDevices?: () => void; tone3000DeviceId?: string } = {}) {
  const hostedCloud = isHostedCloudRuntime();
  const [connectionOpen, setConnectionOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [palette, setPalette] = useState<PaletteId>(() => normalizePalette(localStorage.getItem("ardor-manager.palette")));
  useEffect(() => { localStorage.setItem("ardor-manager.palette", palette); }, [palette]);

  return (
    <SurfaceProvider value={{ palette }}>
      <div className="app-shell" data-palette={palette} style={paletteVariables(palette)}>
        <EditorProvider>
          <Shell tone3000DeviceId={tone3000DeviceId} onCloudDevices={onCloudDevices}
            onConnection={() => setConnectionOpen(true)} onSettings={() => setSettingsOpen(true)} />
        </EditorProvider>
        <ConnectionDialog open={connectionOpen} onOpenChange={setConnectionOpen} />
        {!hostedCloud && settingsOpen && (
          <Suspense fallback={null}>
            <SettingsDialog open onOpenChange={setSettingsOpen} palette={palette} onPaletteChange={setPalette} />
          </Suspense>
        )}
      </div>
    </SurfaceProvider>
  );
}
