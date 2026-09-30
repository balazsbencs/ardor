import { AlertTriangle, Copy, Upload, X } from "lucide-react";

import type { AssetKind } from "../api/types";
import { Button } from "../components/ui";
import type { missingFiles } from "./assetUsage";
import { presetName } from "./kindInfo";
import type { QueueItem } from "./uploadQueue";

type Missing = ReturnType<typeof missingFiles>[number];

const MISSING_HINT = "The preset stays saved, but the pedal cannot load it until the file is back or you pick another one.";
const SUPPORTED = /\.(nam|wav)$/i;

/** One compact warn row per preset that needs a file the pedal does not have. */
export function MissingRows({ missing, onUpload, onPickAnother }: {
  missing: Missing[]; onUpload(): void; onPickAnother(location: { bank: number; slot: number }): void;
}) {
  return <>{missing.flatMap((entry) => entry.presets.map((use) => (
    <div className="amiss" key={`${entry.path}:${use.bank}:${use.slot}`} title={MISSING_HINT}>
      <AlertTriangle size={18} aria-hidden="true" />
      <span><b>{presetName(use)}</b> needs <b>{entry.path.split("/").pop()}</b>, which is not on the pedal.</span>
      <Button className="abtn" onClick={onUpload}><Upload size={14} />Upload</Button>
      <Button className="abtn" onClick={() => onPickAnother({ bank: use.bank, slot: use.slot })}>Pick another</Button>
    </div>
  )))}</>;
}

function QueueRow({ item, onResolve, onDismiss }: {
  item: QueueItem; onResolve(id: number, choice: "replace" | "skip"): void; onDismiss(id: number): void;
}) {
  const name = item.file.name;
  if (item.state === "rejected") {
    const unsupported = !SUPPORTED.test(name);
    return (
      <div className="aq aq--bad" role={unsupported ? "alert" : undefined}>
        <X size={18} aria-hidden="true" />
        <span><b>{name}</b> {unsupported ? "is not a .nam or .wav file. The pedal takes NAM models and WAV impulse responses." : "could not be uploaded."}</span>
        <Button className="abtn" variant="quiet" onClick={() => onDismiss(item.id)}>Dismiss</Button>
      </div>
    );
  }
  if (item.state === "conflict") {
    return (
      <div className="aq aq--warn" role="alert">
        <Copy size={18} aria-hidden="true" />
        <span><b>{name}</b> is already on the pedal. Replace it? Presets that use it get the new file.</span>
        <Button className="abtn" onClick={() => onResolve(item.id, "skip")}>Skip</Button>
        <Button className="abtn" variant="danger" onClick={() => onResolve(item.id, "replace")}>Replace</Button>
      </div>
    );
  }
  return (
    <div className="aq">
      <span className="aq__name"><Upload size={16} aria-hidden="true" /><b>{name}</b></span>
      <span className={item.state === "uploading" ? "aq__bar" : "aq__bar is-waiting"} role="progressbar" aria-label={`Uploading ${name}`}><i /></span>
    </div>
  );
}

export function QueueRows({ queue, kind, onResolve, onDismiss }: {
  queue: QueueItem[]; kind: AssetKind; onResolve(id: number, choice: "replace" | "skip"): void; onDismiss(id: number): void;
}) {
  return <>{queue.filter((item) => item.kind === kind || item.state === "rejected")
    .map((item) => <QueueRow key={item.id} item={item} onResolve={onResolve} onDismiss={onDismiss} />)}</>;
}
