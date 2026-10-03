import { useEffect, useRef, useState, type FormEvent } from "react";

import type { Asset } from "../api/types";
import { Button } from "../components/ui";
import { fileStem } from "../ui/format";
import { plural } from "./kindInfo";

/** Inline rename. The file keeps its type ending; the pedal updates the saved presets. */
export function RenameForm({ asset, extension, usedCount, onSubmit, onCancel }: {
  asset: Asset; extension: string; usedCount?: number;
  onSubmit(filename: string): Promise<string | undefined>; onCancel(): void;
}) {
  const [value, setValue] = useState(asset.filename);
  const [error, setError] = useState<string>();
  const input = useRef<HTMLInputElement>(null);

  useEffect(() => {
    input.current?.focus();
    input.current?.setSelectionRange(0, fileStem(asset.filename).length);
  }, [asset.filename]);

  const submit = async (event: FormEvent) => {
    event.preventDefault();
    setError(await onSubmit(value));
  };
  const note = usedCount === undefined ? ""
    : usedCount > 0 ? ` ${plural(usedCount, "saved preset")} will use the new name.` : " No preset uses this file.";

  return (
    <form className="arename" aria-label="Rename file" onSubmit={(event) => void submit(event)}>
      <label className="search">
        <input ref={input} aria-label="New file name" value={value} onChange={(event) => setValue(event.target.value)}
          onKeyDown={(event) => { if (event.key === "Escape") { event.stopPropagation(); onCancel(); } }} />
      </label>
      <Button type="submit" variant="primary">Rename</Button>
      <Button type="button" variant="quiet" onClick={onCancel}>Cancel</Button>
      <p className="lb-note">{error ? <span className="aerr" role="alert">{error}</span> : `Keep the ${extension} ending.${note}`}</p>
    </form>
  );
}
