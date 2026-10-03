import { Check, Pencil, Trash2 } from "lucide-react";

import type { Asset, AssetKind, AssetUsageEntry } from "../api/types";
import { cx } from "../components/ui";
import { fileSize, fileStem } from "../ui/format";
import { usedBy } from "./assetUsage";
import { KIND_INFO, presetName } from "./kindInfo";

const CHIPS_SHOWN = 3;

function Checkbox({ checked, label, onToggle }: { checked: boolean; label: string; onToggle(): void }) {
  return (
    <button type="button" role="checkbox" aria-checked={checked} aria-label={label} className="acheck" onClick={onToggle}>
      {checked && <Check size={14} strokeWidth={3} aria-hidden="true" />}
    </button>
  );
}

function UsedIn({ uses }: { uses: NonNullable<ReturnType<typeof usedBy>> }) {
  if (uses.length === 0) return <span className="aused"><span className="unused">Not used</span></span>;
  return (
    <span className="aused">
      {uses.slice(0, CHIPS_SHOWN).map((use) => <span className="pchip" key={`${use.bank}:${use.slot}`}>{presetName(use)}</span>)}
      {uses.length > CHIPS_SHOWN && <span className="pchip pchip--more">+{uses.length - CHIPS_SHOWN}</span>}
    </span>
  );
}

export function FileRows({ kind, files, allFiles, query, usage, checked, openId, onToggle, onToggleAll, onOpen, onRename, onDelete }: {
  kind: AssetKind; files: Asset[]; allFiles: Asset[]; query: string; usage: AssetUsageEntry[] | undefined;
  checked: ReadonlySet<string>; openId?: string;
  onToggle(id: string): void; onToggleAll(): void; onOpen(asset: Asset): void; onRename(asset: Asset): void; onDelete(asset: Asset): void;
}) {
  const info = KIND_INFO[kind];
  if (allFiles.length === 0) {
    return (
      <div className="aempty">
        <span className={cx("akind__code", `fam-${info.family}`)}>{info.code}</span>
        <h3>No {info.label.toLowerCase()} yet</h3>
        <p>Drop {info.extension} files anywhere on this page, or press Upload. Then pick them in a {info.block} block.</p>
      </div>
    );
  }
  if (files.length === 0) {
    return <div className="aempty"><h3>No file matches {"“"}{query}{"”"}</h3><p>Search looks at the file name.</p></div>;
  }
  const all = files.every(({ id }) => checked.has(id));
  return (
    <div className={cx("alist__grid", `fam-${info.family}`)} data-usage={usage ? "on" : "off"}>
      <div className="arow arow--head">
        <Checkbox checked={all} label="Select all files" onToggle={onToggleAll} />
        <span>Name</span><span>Size</span>{usage && <span>Used in</span>}<span />
      </div>
      {files.map((asset) => {
        const on = checked.has(asset.id);
        const uses = usedBy(usage, asset.path);
        return (
          <div key={asset.id} className={cx("arow", openId === asset.id && "is-open", on && "is-checked")}>
            <Checkbox checked={on} label={`Select ${asset.filename}`} onToggle={() => onToggle(asset.id)} />
            <button type="button" className="aname" aria-label={fileStem(asset.filename)} aria-pressed={openId === asset.id} onClick={() => onOpen(asset)}>
              <span className="sq" /><b>{fileStem(asset.filename)}</b><small>{info.extension}</small>
            </button>
            <span className="asize">{fileSize(asset.sizeBytes)}</span>
            {uses && <UsedIn uses={uses} />}
            <span className="aact">
              <button type="button" className="aicon" aria-label={`Rename ${asset.filename}`} title="Rename" onClick={() => onRename(asset)}><Pencil size={15} aria-hidden="true" /></button>
              <button type="button" className="aicon" aria-label={`Delete ${asset.filename}`} title="Delete" onClick={() => onDelete(asset)}><Trash2 size={15} aria-hidden="true" /></button>
            </span>
          </div>
        );
      })}
    </div>
  );
}
