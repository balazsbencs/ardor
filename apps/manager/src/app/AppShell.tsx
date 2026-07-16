import { Cable, FolderOpen, Moon, SlidersHorizontal, Sun } from "lucide-react";
import { useEffect, useState } from "react";

import { AssetLibrary } from "../assets/AssetLibrary";
import { Button, StatusBadge } from "../components/ui";
import { ConnectionDialog } from "../connection/ConnectionDialog";
import { useDeviceSession } from "../connection/deviceSession";
import { PresetWorkspace } from "../presets/workspace/PresetWorkspace";

type View = "workspace" | "assets";

export function AppShell() {
  const session = useDeviceSession();
  const [view, setView] = useState<View>("workspace");
  const [connectionOpen, setConnectionOpen] = useState(false);
  const [theme, setTheme] = useState<"dark" | "light">(() => localStorage.getItem("ardor-manager.theme") === "light" ? "light" : "dark");
  useEffect(() => { localStorage.setItem("ardor-manager.theme", theme); }, [theme]);

  return (
    <div className="app-shell" data-theme={theme}>
      <header className="app-topbar">
        <div className="brand"><span className="brand-mark"><SlidersHorizontal size={19} /></span><span><strong>Ardor</strong><small>Manager</small></span></div>
        <nav className="app-navigation" aria-label="App navigation"><button className={view === "workspace" ? "is-active" : ""} onClick={() => setView("workspace")}>Workspace</button><button className={view === "assets" ? "is-active" : ""} onClick={() => setView("assets")}><FolderOpen size={15} /> Assets</button></nav>
        <div className="topbar-actions"><button className="connection-status" onClick={() => setConnectionOpen(true)}><Cable size={15} />{session.status === "connected" ? <StatusBadge tone="success">{session.device?.deviceName ?? "Connected"}</StatusBadge> : <StatusBadge tone={session.status === "error" ? "danger" : "neutral"}>{session.status === "error" ? "Connection error" : "Disconnected"}</StatusBadge>}</button><Button variant="quiet" className="theme-button" onClick={() => setTheme((current) => current === "dark" ? "light" : "dark")}>{theme === "dark" ? <Sun size={16} /> : <Moon size={16} />}<span>{theme === "dark" ? "Light" : "Dark"}</span></Button></div>
      </header>
      {view === "workspace" ? <PresetWorkspace onAssets={() => setView("assets")} onConnection={() => setConnectionOpen(true)} /> : <AssetLibrary />}
      <ConnectionDialog open={connectionOpen} onOpenChange={setConnectionOpen} />
    </div>
  );
}
