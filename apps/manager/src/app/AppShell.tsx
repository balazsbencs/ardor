import { ArrowLeft, Cable, FolderOpen, Moon, Settings, SlidersHorizontal, Sun } from "lucide-react";
import { type CSSProperties, useEffect, useState } from "react";

import { AssetLibrary } from "../assets/AssetLibrary";
import { Button, IconButton, StatusBadge } from "../components/ui";
import { ConnectionDialog } from "../connection/ConnectionDialog";
import { useDeviceSession } from "../connection/deviceSession";
import { PresetWorkspace } from "../presets/workspace/PresetWorkspace";
import { SettingsDialog, defaultAccent } from "../settings/SettingsDialog";
import { isHostedCloudRuntime } from "../runtime/platform";

type View = "workspace" | "assets";

export function AppShell({ onCloudDevices, tone3000DeviceId }: { onCloudDevices?: () => void; tone3000DeviceId?: string } = {}) {
  const session = useDeviceSession();
  const hostedCloud = isHostedCloudRuntime();
  const [view, setView] = useState<View>("workspace");
  const [connectionOpen, setConnectionOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [theme, setTheme] = useState<"dark" | "light">(() => localStorage.getItem("ardor-manager.theme") === "light" ? "light" : "dark");
  const [accent, setAccent] = useState(() => localStorage.getItem("ardor-manager.accent") ?? defaultAccent);
  useEffect(() => { localStorage.setItem("ardor-manager.theme", theme); }, [theme]);
  useEffect(() => { localStorage.setItem("ardor-manager.accent", accent); }, [accent]);
  const accentStyle = {
    "--accent": accent,
    "--focus": accent,
    "--accent-ink": parseInt(accent.slice(1, 3), 16) * .299
      + parseInt(accent.slice(3, 5), 16) * .587
      + parseInt(accent.slice(5, 7), 16) * .114 > 150 ? "#0a0d0b" : "#ffffff",
  } as CSSProperties;

  return (
    <div className="app-shell" data-theme={theme} style={accentStyle}>
      <header className="app-topbar">
        <div className="brand"><span className="brand-mark"><SlidersHorizontal size={19} /></span><span><strong>Ardor</strong><small>Manager</small></span></div>
        <nav className="app-navigation" aria-label="App navigation"><button className={view === "workspace" ? "is-active" : ""} onClick={() => setView("workspace")}>Workspace</button><button className={view === "assets" ? "is-active" : ""} onClick={() => setView("assets")}><FolderOpen size={15} /> Assets</button></nav>
        <div className="topbar-actions">{hostedCloud && onCloudDevices && <Button variant="quiet" onClick={onCloudDevices}><ArrowLeft size={15} /> Devices</Button>}<button className="connection-status" onClick={() => setConnectionOpen(true)}><Cable size={15} />{session.status === "connected" ? <StatusBadge tone="success">{session.device?.deviceName ?? "Connected"}</StatusBadge> : <StatusBadge tone={session.status === "error" ? "danger" : "neutral"}>{session.status === "error" ? "Connection error" : "Disconnected"}</StatusBadge>}</button><Button variant="quiet" className="theme-button" onClick={() => setTheme((current) => current === "dark" ? "light" : "dark")}>{theme === "dark" ? <Sun size={16} /> : <Moon size={16} />}<span>{theme === "dark" ? "Light" : "Dark"}</span></Button>{!hostedCloud && <IconButton label="Open settings" onClick={() => setSettingsOpen(true)}><Settings size={17} /></IconButton>}</div>
      </header>
      {view === "workspace" ? <PresetWorkspace onAssets={() => setView("assets")} onConnection={() => setConnectionOpen(true)} /> : <AssetLibrary tone3000DeviceId={tone3000DeviceId} />}
      <ConnectionDialog open={connectionOpen} onOpenChange={setConnectionOpen} />
      {!hostedCloud && <SettingsDialog open={settingsOpen} onOpenChange={setSettingsOpen} accent={accent} theme={theme} onAccentChange={setAccent} />}
    </div>
  );
}
