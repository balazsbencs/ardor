import { Pencil, Send, Trash2, Upload, X } from "lucide-react";

import type { Asset, AssetKind, AssetUsageEntry } from "../api/types";
import { Button, IconButton, cx } from "../components/ui";
import { useDeviceSession } from "../connection/deviceSession";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { PresetTile } from "../stage/PresetTile";
import { bankLabel, fileSize, fileStem } from "../ui/format";
import { usedBy } from "./assetUsage";
import { KIND_INFO, plural } from "./kindInfo";
import { RenameForm } from "./RenameForm";
import { useUsedPresets } from "./useUsedPresets";
import "../stage/drawer.css";

const TYPE_KIND: Record<string, AssetKind> = { nam: "models", cab: "irs", irreverb: "reverb-irs" };

function TryInPreset({ asset, kind }: { asset: Asset; kind: AssetKind }) {
  const editor = usePresetEditorContext();
  const block = editor.allBlocks.find((candidate) => TYPE_KIND[candidate.type] === kind);
  const name = editor.present.name;
  const inUse = block?.asset === asset.path;
  // "Try in preset" is an editor edit, so Undo takes it back. File changes on the pedal are not undoable.
  return (
    <Button variant={inUse ? "secondary" : "primary"} disabled={!block || inUse}
      title={block ? undefined : `${name} has no ${KIND_INFO[kind].block} block`}
      onClick={() => block && editor.dispatch({ type: "set-block-asset", blockId: block.id, asset: asset.path })}>
      <Send size={15} aria-hidden="true" />{inUse ? `In ${name}` : `Try in ${name}`}
    </Button>
  );
}

function UsedIn({ uses, block, onOpenPreset }: { uses: NonNullable<ReturnType<typeof usedBy>>; block: string; onOpenPreset(location: { bank: number; slot: number }): void }) {
  const session = useDeviceSession();
  const editor = usePresetEditorContext();
  const presetOf = useUsedPresets(uses);
  if (uses.length === 0) {
    return <div className="ausers"><h3>Not used in a preset</h3><p className="lb-note">You can delete it and no preset changes. Or pick it in a {block} block.</p></div>;
  }
  const live = session.device?.active;
  return (
    <div className="ausers">
      <h3>Used in {plural(uses.length, "preset")}</h3>
      <div className="ausers__tiles">
        {uses.map((use) => {
          const editing = editor.editor.location.bank === use.bank && editor.editor.location.slot === use.slot;
          return (
            <div className="ausers__item" key={`${use.bank}:${use.slot}`}>
              <span className="ausers__bank">{bankLabel(use.bank)}</span>
              <PresetTile slot={use.slot} preset={presetOf(use)} live={live?.bank === use.bank && live.slot === use.slot}
                editing={editing} dirty={editing && editor.dirty} missing={false} onOpen={() => onOpenPreset({ bank: use.bank, slot: use.slot })} />
            </div>
          );
        })}
      </div>
    </div>
  );
}

export function FileDrawer({ asset, kind, usage, renaming, onRenameStart, onRenameSubmit, onRenameCancel, onReplace, onDelete, onClose, onOpenPreset }: {
  asset: Asset; kind: AssetKind; usage: AssetUsageEntry[] | undefined; renaming: boolean;
  onRenameStart(): void; onRenameSubmit(filename: string): Promise<string | undefined>; onRenameCancel(): void;
  onReplace(): void; onDelete(): void; onClose(): void; onOpenPreset(location: { bank: number; slot: number }): void;
}) {
  const info = KIND_INFO[kind];
  const uses = usedBy(usage, asset.path);
  return (
    <section className={cx("drawer", "adrawer", `fam-${info.family}`)} aria-label={asset.filename}>
      <div className="drawer__head">
        <span className="atag">{info.one}</span>
        <h2>{fileStem(asset.filename)}</h2>
        <span className="drawer__sub">{asset.path} · {fileSize(asset.sizeBytes)}</span>
        <div className="drawer__actions">
          <TryInPreset asset={asset} kind={kind} />
          <Button onClick={onRenameStart}><Pencil size={15} aria-hidden="true" />Rename</Button>
          <Button onClick={onReplace}><Upload size={15} aria-hidden="true" />Replace file</Button>
          <Button variant="danger" onClick={onDelete}><Trash2 size={15} aria-hidden="true" />Delete</Button>
          <IconButton label="Close" onClick={onClose}><X size={16} aria-hidden="true" /></IconButton>
        </div>
      </div>
      {renaming && <RenameForm key={asset.id} asset={asset} extension={info.extension} usedCount={uses?.length}
        onSubmit={onRenameSubmit} onCancel={onRenameCancel} />}
      {uses && <UsedIn uses={uses} block={info.block} onOpenPreset={onOpenPreset} />}
    </section>
  );
}
