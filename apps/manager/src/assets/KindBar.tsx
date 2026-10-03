import type { Asset, AssetKind } from "../api/types";
import { cx } from "../components/ui";
import { ASSET_KINDS, KIND_INFO, plural, totalSize } from "./kindInfo";

type Inventory = { models: Asset[]; irs: Asset[]; reverbIrs: Asset[] };

const filesOf = (inventory: Inventory, kind: AssetKind): Asset[] =>
  kind === "models" ? inventory.models : kind === "irs" ? inventory.irs : inventory.reverbIrs;
const bytesOf = (files: Asset[]) => files.reduce((sum, { sizeBytes }) => sum + sizeBytes, 0);

export function KindBar({ kind, onKind, inventory, showReverb, missingCount }: {
  kind: AssetKind; onKind(kind: AssetKind): void; inventory: Inventory; showReverb: boolean; missingCount: number;
}) {
  const kinds = ASSET_KINDS.filter((item) => item !== "reverb-irs" || showReverb);
  const all = kinds.flatMap((item) => filesOf(inventory, item));
  return (
    <section className="akindbar" aria-label="File type">
      <div className="akinds">
        {kinds.map((item) => {
          const info = KIND_INFO[item];
          const files = filesOf(inventory, item);
          return (
            <button key={item} type="button" className={cx("akind", `fam-${info.family}`)} aria-pressed={kind === item} onClick={() => onKind(item)}>
              <span className="akind__code">{info.code}</span>
              <span className="akind__txt"><b>{info.label}</b><small>{info.extension} · for {info.block}</small></span>
              <span className="akind__n">{files.length}<small>{totalSize(bytesOf(files))}</small></span>
            </button>
          );
        })}
      </div>
      <div className="asum">
        <b>{plural(all.length, "file")} on the pedal</b>
        <span>{totalSize(bytesOf(all))}{missingCount > 0 && <> · <em>{missingCount} missing</em></>}</span>
      </div>
    </section>
  );
}
