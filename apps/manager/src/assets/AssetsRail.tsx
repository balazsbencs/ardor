import { ChevronLeft, Search, Trash2, Upload } from "lucide-react";

import type { Asset } from "../api/types";
import { Button } from "../components/ui";
import { usedBy } from "./assetUsage";
import { KIND_INFO, plural, presetName } from "./kindInfo";
import type { AssetLibrary } from "./useAssetLibrary";

function DeleteQuestion({ targets, library }: { targets: Asset[]; library: AssetLibrary }) {
  const one = targets.length === 1;
  const it = one ? "it" : "them";
  const uses = targets.map((asset) => usedBy(library.usage, asset.path));
  const presets = [...new Set(uses.flatMap((list) => list ?? []).map(presetName))];
  const verb = presets.length === 1 ? "uses" : "use";
  const stay = "Those presets stay saved but cannot load until you pick another file.";
  const consequence = library.usage === undefined ? `Presets that use ${it} stay saved but cannot load until you pick another file.`
    : presets.length > 0 ? `${presets.join(", ")} ${verb} ${it}. ${stay}` : `No preset uses ${it}.`;
  return (
    <div className="aconfirm" role="alertdialog" aria-label="Confirm delete">
      <Trash2 size={18} aria-hidden="true" />
      <span><b>Delete {one ? targets[0].filename : plural(targets.length, "file")} from the pedal?</b> {consequence} You cannot undo this.</span>
      <Button autoFocus onClick={library.cancelDelete}>Cancel</Button>
      <Button variant="danger" onClick={() => void library.deleteChecked()}>Delete {one ? "file" : plural(targets.length, "file")}</Button>
    </div>
  );
}

export function AssetsRail({ library, canBrowseTone3000, onPick, onBrowseTone3000, onBack }: {
  library: AssetLibrary; canBrowseTone3000: boolean; onPick(): void; onBrowseTone3000(): void; onBack?(): void;
}) {
  const count = library.checked.size;
  const targets = library.files.filter(({ id }) => library.checked.has(id));
  const asking = library.confirmDelete && targets.length > 0;
  return (
    <nav className="rail" aria-label="File actions">
      {asking ? <DeleteQuestion targets={targets} library={library} /> : (
        <>
          <Button variant="primary" onClick={onPick}><Upload size={16} aria-hidden="true" /><span className="lbl">Upload {KIND_INFO[library.kind].extension}</span></Button>
          {canBrowseTone3000 && <Button onClick={onBrowseTone3000}><Search size={16} aria-hidden="true" /><span className="lbl">Browse TONE3000</span></Button>}
          {count > 0 && (
            <>
              <span className="acount">{count} selected</span>
              <Button variant="danger" onClick={() => library.askDelete()}><Trash2 size={16} aria-hidden="true" /><span className="lbl">Delete {count}</span></Button>
              <Button variant="quiet" onClick={library.clearChecked}>Clear</Button>
            </>
          )}
          <span className="rail__push" />
          <Button onClick={onBack}><ChevronLeft size={16} aria-hidden="true" /><span className="lbl">Back to edit</span></Button>
        </>
      )}
    </nav>
  );
}
