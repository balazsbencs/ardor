import { Search } from "lucide-react";

import type { Asset } from "../api/types";
import { cx } from "../components/ui";
import { MissingRows, QueueRows } from "./AssetNotices";
import { FileRows } from "./FileRows";
import { KIND_INFO } from "./kindInfo";
import type { AssetSort } from "./libraryView";
import type { AssetLibrary } from "./useAssetLibrary";

const SORTS: Array<[AssetSort, string]> = [["name", "Name"], ["size", "Size"], ["used", "Most used"]];

export function FileList({ library, extraError, onPick, onRename, onOpenPreset }: {
  library: AssetLibrary; extraError?: string;
  onPick(): void; onRename(asset: Asset): void; onOpenPreset(location: { bank: number; slot: number }): void;
}) {
  const { kind, usage } = library;
  const info = KIND_INFO[kind];
  const error = library.error ?? extraError;
  return (
    <div className="astage">
      <div className="atools">
        <label className="search"><Search size={16} aria-hidden="true" />
          <input type="search" aria-label="Find a file" placeholder={`Find a file in ${info.label}`} value={library.query} onChange={(event) => library.setQuery(event.target.value)} />
        </label>
        <div className="seg" role="group" aria-label="Sort">
          {SORTS.filter(([key]) => key !== "used" || usage !== undefined).map(([key, label]) =>
            <button key={key} type="button" className="btn btn--sm" aria-pressed={library.sort === key} onClick={() => library.setSort(key)}>{label}</button>)}
        </div>
        <span className="ahint">Drop {info.extension} files anywhere to upload</span>
      </div>
      {library.notice && <p className="anote" role="status">{library.notice}</p>}
      {error && <p className={cx("anote", "anote--bad")} role="alert">{error}</p>}
      <MissingRows missing={library.missing.filter((entry) => entry.kind === kind)} onUpload={onPick} onPickAnother={onOpenPreset} />
      <QueueRows queue={library.queue} kind={kind} onResolve={library.resolve} onDismiss={library.dismiss} />
      <div className="alist">
        <FileRows kind={kind} files={library.visible} allFiles={library.files} query={library.query} usage={usage}
          checked={library.checked} openId={library.openId} onToggle={library.toggleChecked} onToggleAll={library.toggleAll}
          onOpen={(asset) => library.setOpenId(library.openId === asset.id ? undefined : asset.id)}
          onRename={onRename} onDelete={(asset) => library.askDelete([asset.id])} />
      </div>
    </div>
  );
}
