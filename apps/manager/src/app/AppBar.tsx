import { ArrowLeft, FolderOpen, Settings, SlidersHorizontal } from "lucide-react";

import { Button, IconButton } from "../components/ui";
import { useDeviceSession } from "../connection/deviceSession";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { isHostedCloudRuntime } from "../runtime/platform";
import { LiveState } from "../stage/LiveState";
import { bankLabel, slotLabel } from "../ui/format";
import { Tag } from "../ui/Tag";
import { ConnectionPill } from "./ConnectionPill";
import type { AppView } from "./useAppView";

type Props = {
  view: AppView;
  onView(view: AppView): void;
  onConnection(): void;
  onSettings(): void;
  onCloudDevices?: () => void;
};

/** Mark, Edit / Assets switch, the open preset, live state, connection and Settings. */
export function AppBar({ view, onView, onConnection, onSettings, onCloudDevices }: Props) {
  const editor = usePresetEditorContext();
  const session = useDeviceSession();
  const hosted = isHostedCloudRuntime();
  const connected = session.status === "connected" && Boolean(session.current);
  const { bank, slot } = editor.editor.location;
  return (
    <header className="appbar">
      <span className="mark"><i aria-hidden="true" />Ardor</span>
      <div className="seg viewseg" role="group" aria-label="View">
        <button type="button" className="btn btn--sm" aria-pressed={view === "edit"} onClick={() => onView("edit")}>
          <SlidersHorizontal size={16} aria-hidden="true" />Edit
        </button>
        <button type="button" className="btn btn--sm" aria-pressed={view === "assets"} onClick={() => onView("assets")}>
          <FolderOpen size={16} aria-hidden="true" />Assets
        </button>
      </div>
      {connected && (
        <div className="where"><b>{editor.present.name}</b><span>{bankLabel(bank)} · {slotLabel(slot)}</span></div>
      )}
      {connected && editor.dirty && <span className="appbar__dirty"><Tag tone="warn">MODIFIED</Tag></span>}
      <span className="appbar__push" />
      {connected && view === "edit" && <span className="appbar__live"><LiveState /></span>}
      <ConnectionPill onOpen={onConnection} />
      {hosted && onCloudDevices && <Button variant="quiet" onClick={onCloudDevices}><ArrowLeft size={15} />Devices</Button>}
      {!hosted && <IconButton label="Open settings" onClick={onSettings}><Settings size={17} /></IconButton>}
    </header>
  );
}
