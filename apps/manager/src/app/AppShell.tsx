import { Suspense, lazy, useEffect, useState } from "react";

import { AssetLibrary } from "../assets/AssetLibrary";
import { ConnectionDialog } from "../connection/ConnectionDialog";
import { EditorProvider } from "../presets/editor/EditorContext";
import { StageWorkspace } from "../stage/StageWorkspace";
import { normalizePalette, paletteVariables, type PaletteId } from "../theme/accent";
import { SurfaceProvider } from "../theme/surface";
import { isHostedCloudRuntime } from "../runtime/platform";
import { AppBar } from "./AppBar";
import { useAppView } from "./useAppView";
import "./app.css";

const SettingsDialog = lazy(() => import("../settings/SettingsDialog").then((module) => ({ default: module.SettingsDialog })));

export function AppShell({ onCloudDevices, tone3000DeviceId }: { onCloudDevices?: () => void; tone3000DeviceId?: string } = {}) {
  const hostedCloud = isHostedCloudRuntime();
  const { view, goto } = useAppView();
  const [connectionOpen, setConnectionOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [palette, setPalette] = useState<PaletteId>(() => normalizePalette(localStorage.getItem("ardor-manager.palette")));
  useEffect(() => { localStorage.setItem("ardor-manager.palette", palette); }, [palette]);
  const openConnection = () => setConnectionOpen(true);

  return (
    <SurfaceProvider value={{ palette }}>
      <div className="app-shell" data-palette={palette} style={paletteVariables(palette)}>
        <EditorProvider>
          <AppBar view={view} onView={(next) => goto(next)} onConnection={openConnection}
            onSettings={() => setSettingsOpen(true)} onCloudDevices={onCloudDevices} />
          {view === "edit"
            ? <StageWorkspace onManageFiles={(kind) => goto("assets", kind)} onConnection={openConnection} />
            : <div className="app-view"><AssetLibrary tone3000DeviceId={tone3000DeviceId} /></div>}
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
