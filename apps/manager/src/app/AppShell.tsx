import { ArrowLeft, Cable, FolderOpen, Palette, Settings, SlidersHorizontal } from "lucide-react";
import { Suspense, lazy, useEffect, useState } from "react";

import { AssetLibrary } from "../assets/AssetLibrary";
import { Button, IconButton, StatusBadge } from "../components/ui";
import { ConnectionDialog } from "../connection/ConnectionDialog";
import { useDeviceSession } from "../connection/deviceSession";
import { PresetWorkspace } from "../presets/workspace/PresetWorkspace";
import { normalizePalette, paletteById, paletteVariables, palettes, type PaletteId } from "../theme/accent";
import { SurfaceProvider } from "../theme/surface";
import { isHostedCloudRuntime } from "../runtime/platform";

const SettingsDialog = lazy(() => import("../settings/SettingsDialog").then((module) => ({ default: module.SettingsDialog })));

type View = "workspace" | "assets";

export function AppShell({ onCloudDevices, tone3000DeviceId }: { onCloudDevices?: () => void; tone3000DeviceId?: string } = {}) {
  const session = useDeviceSession();
  const hostedCloud = isHostedCloudRuntime();
  const [view, setView] = useState<View>("workspace");
  const [connectionOpen, setConnectionOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [palette, setPalette] = useState<PaletteId>(() => normalizePalette(localStorage.getItem("ardor-manager.palette")));
  useEffect(() => { localStorage.setItem("ardor-manager.palette", palette); }, [palette]);
  const nextPalette = () => {
    const index = palettes.findIndex((entry) => entry.id === palette);
    setPalette(palettes[(index + 1) % palettes.length].id);
  };

  return (
    <SurfaceProvider value={{ palette }}>
      <div className="app-shell" data-palette={palette} style={paletteVariables(palette)}>
        <header className="app-topbar">
          <div className="brand"><span className="brand-mark"><SlidersHorizontal size={19} /></span><span><strong>Ardor</strong><small>Manager</small></span></div>
          <nav className="app-navigation" aria-label="App navigation"><button className={view === "workspace" ? "is-active" : ""} onClick={() => setView("workspace")}>Workspace</button><button className={view === "assets" ? "is-active" : ""} onClick={() => setView("assets")}><FolderOpen size={15} /> Assets</button></nav>
          <div className="topbar-actions">{hostedCloud && onCloudDevices && <Button variant="quiet" onClick={onCloudDevices}><ArrowLeft size={15} /> Devices</Button>}<button className="connection-status" onClick={() => setConnectionOpen(true)} aria-label={`Device: ${session.status === "connected" ? session.device?.deviceName ?? "Connected" : session.status === "error" ? "Connection error" : "Disconnected"}`} title="Open device connection"><Cable size={15} />{session.status === "connected" ? <StatusBadge tone="success">{session.device?.deviceName ?? "Connected"}</StatusBadge> : <StatusBadge tone={session.status === "error" ? "danger" : "neutral"}>{session.status === "error" ? "Connection error" : "Disconnected"}</StatusBadge>}</button><Button variant="quiet" className="palette-button" onClick={nextPalette} title="Cycle panel palette"><Palette size={16} /><span>{paletteById(palette).name}</span></Button>{!hostedCloud && <IconButton label="Open settings" onClick={() => setSettingsOpen(true)}><Settings size={17} /></IconButton>}</div>
        </header>
        {view === "workspace" ? <PresetWorkspace onAssets={() => setView("assets")} onConnection={() => setConnectionOpen(true)} /> : <AssetLibrary tone3000DeviceId={tone3000DeviceId} />}
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
