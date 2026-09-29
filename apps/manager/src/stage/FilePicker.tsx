import { FolderOpen } from "lucide-react";

import type { AssetKind } from "../api/types";
import { useDeviceSession } from "../connection/deviceSession";
import { fileStem } from "../ui/format";

const LABELS: Record<AssetKind, string> = { models: "NAM model", irs: "Cabinet IR", "reverb-irs": "Reverb IR" };

export function FilePicker({ label, kind, value, onChange, onManage }: { label?: string; kind: AssetKind; value: string; onChange(path: string): void; onManage(): void }) {
  const session = useDeviceSession();
  const files = kind === "models" ? session.models : kind === "irs" ? session.irs : session.reverbIrs;
  const missing = value !== "" && !files.some(({ path }) => path === value);
  return (
    <div className="lb-ctl lb-ctl--wide">
      <div className="lb-ctl__top"><span className="lb-ctl__label">{label ?? LABELS[kind]}</span>
        <button type="button" className="lb-ctl__share" onClick={onManage}><FolderOpen size={12} /> Manage files</button></div>
      {missing && <p className="lb-missing">{value.split("/").pop()} is not on the pedal. Pick another file, or upload it in Assets.</p>}
      <div className="lb-choice" role="radiogroup" aria-label={label ?? LABELS[kind]}>
        {files.map((file) => <button key={file.id} type="button" role="radio" aria-checked={file.path === value} onClick={() => onChange(file.path)}>{fileStem(file.filename)}</button>)}
        {files.length === 0 && <span className="lb-note">No files yet. Use Manage files to upload one.</span>}
      </div>
    </div>
  );
}
