import { FileAudio, Music2, Pencil, Trash2, Upload } from "lucide-react";
import { useRef, useState } from "react";

import type { Asset, AssetKind } from "../api/types";
import { ArdorApiError } from "../api/errors";
import { Button, IconButton, StatusBadge } from "../components/ui";
import { useDeviceSession } from "../connection/deviceSession";

function size(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

export function AssetLibrary() {
  const session = useDeviceSession();
  const [kind, setKind] = useState<AssetKind>("models");
  const [query, setQuery] = useState("");
  const [error, setError] = useState<string>();
  const [notice, setNotice] = useState<string>();
  const [pendingDelete, setPendingDelete] = useState<Asset>();
  const [conflict, setConflict] = useState<{ file: File; kind: AssetKind; remaining: File[] }>();
  const [renaming, setRenaming] = useState<Asset>();
  const [renameValue, setRenameValue] = useState("");
  const fileRef = useRef<HTMLInputElement>(null);
  const assets = kind === "models" ? session.models : session.irs;
  const visible = assets.filter((asset) => asset.filename.toLowerCase().includes(query.toLowerCase()));

  const clearFileInput = () => { if (fileRef.current) fileRef.current.value = ""; };
  const uploadSequentially = async (uploadKind: AssetKind, files: File[]) => {
    if (files.length === 0) {
      clearFileInput();
      return;
    }
    setError(undefined);
    setNotice(undefined);
    for (const [index, file] of files.entries()) {
      const expected = uploadKind === "models" ? ".nam" : ".wav";
      if (!file.name.toLowerCase().endsWith(expected)) {
        setError(`${file.name} is not a ${expected} file.`);
        continue;
      }
      try {
        const response = await session.uploadAsset(uploadKind, file, false);
        if (response) {
          await session.refreshAssets(uploadKind);
          setNotice(`${file.name} uploaded.`);
        }
      } catch (reason) {
        if (reason instanceof ArdorApiError && reason.code === "asset_exists") {
          setConflict({ file, kind: uploadKind, remaining: files.slice(index + 1) });
          return;
        }
        setError(reason instanceof Error ? reason.message : `Could not upload ${file.name}.`);
      }
    }
    clearFileInput();
  };

  const upload = (files: FileList | File[] | null) => {
    if (!files) return;
    void uploadSequentially(kind, Array.from(files));
  };

  const resolveConflict = async (choice: "replace" | "skip") => {
    const pending = conflict;
    setConflict(undefined);
    if (!pending) return;
    if (choice === "replace") {
      setError(undefined);
      try {
        const response = await session.uploadAsset(pending.kind, pending.file, true);
        if (response) {
          await session.refreshAssets(pending.kind);
          setNotice(`${pending.file.name} replaced.`);
        }
      } catch (reason) {
        setError(reason instanceof Error ? reason.message : `Could not replace ${pending.file.name}.`);
      }
    }
    await uploadSequentially(pending.kind, pending.remaining);
  };

  const remove = async () => {
    if (!pendingDelete || !session.client) return;
    try {
      await session.client.deleteAsset(kind, pendingDelete.id);
      await session.refreshAssets(kind);
      setPendingDelete(undefined);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not delete asset.");
    }
  };

  const beginRename = (asset: Asset) => {
    setError(undefined);
    setRenameValue(asset.filename);
    setRenaming(asset);
  };

  const rename = async () => {
    if (!renaming || !session.client) return;
    const filename = renameValue.trim();
    if (!filename) {
      setError("A filename is required.");
      return;
    }
    try {
      const response = await session.client.renameAsset(kind, renaming.id, filename);
      await session.refreshAssets(kind);
      if (session.current) await session.selectLocation(session.current.location);
      setNotice(`${renaming.filename} renamed to ${response.asset.filename}.${response.updatedPresetCount > 0 ? ` Updated ${response.updatedPresetCount} saved preset${response.updatedPresetCount === 1 ? "" : "s"}.` : ""}`);
      setRenaming(undefined);
    } catch (reason) {
      if (reason instanceof ArdorApiError && reason.code === "asset_exists") {
        setError("An asset with that filename already exists.");
      } else {
        setError(reason instanceof Error ? reason.message : "Could not rename asset.");
      }
    }
  };

  if (session.status !== "connected") return <main className="assets-view"><div className="assets-empty"><Music2 size={36} /><h1>Connect to manage assets</h1><p>Models and cabinet IRs live on the pedal and are available to preset blocks after upload.</p></div></main>;

  return (
    <main className="assets-view">
      <header className="assets-view__header"><div><p className="eyebrow">Device assets</p><h1>Models & cabinet IRs</h1><p>Upload files once, then choose them from the relevant block inspector.</p></div><Button onClick={() => fileRef.current?.click()} disabled={session.busy.upload || conflict !== undefined}><Upload size={16} /> {session.busy.upload ? "Uploading…" : "Upload files"}</Button><input ref={fileRef} hidden type="file" multiple accept={kind === "models" ? ".nam" : ".wav"} onChange={(event) => upload(event.target.files)} /></header>
      <div className="assets-toolbar"><div className="category-tabs" role="tablist" aria-label="Asset type"><button role="tab" aria-selected={kind === "models"} className={kind === "models" ? "is-active" : ""} onClick={() => setKind("models")}>NAM models</button><button role="tab" aria-selected={kind === "irs"} className={kind === "irs" ? "is-active" : ""} onClick={() => setKind("irs")}>Cabinet IRs</button></div><input className="asset-search" placeholder="Search files" value={query} onChange={(event) => setQuery(event.target.value)} /></div>
      <div className="asset-dropzone" onDragOver={(event) => event.preventDefault()} onDrop={(event) => { event.preventDefault(); upload(event.dataTransfer.files); }}><Upload size={16} /><span>Drop {kind === "models" ? ".nam" : ".wav"} files here to upload</span></div>
      {error && <p className="assets-error" role="alert">{error}</p>}
      {notice && <p className="assets-notice" role="status">{notice}</p>}
      <section className="asset-table" aria-label={kind === "models" ? "NAM models" : "Cabinet IRs"}>{visible.map((asset) => <article className="asset-row" key={asset.id}><span className="asset-row__icon">{kind === "models" ? <Music2 size={18} /> : <FileAudio size={18} />}</span><div><strong>{asset.filename}</strong><small>{asset.path}</small></div><span>{size(asset.sizeBytes)}</span><IconButton label={`Rename ${asset.filename}`} onClick={() => beginRename(asset)}><Pencil size={15} /></IconButton><IconButton label={`Delete ${asset.filename}`} onClick={() => setPendingDelete(asset)}><Trash2 size={16} /></IconButton></article>)}{visible.length === 0 && <div className="assets-empty"><FileAudio size={32} /><h2>No {kind === "models" ? "models" : "IRs"} yet</h2><p>Upload a {kind === "models" ? ".nam model" : ".wav cabinet impulse response"} to use it in a preset.</p></div>}</section>
      {pendingDelete && <div className="confirm-popover" role="alertdialog" aria-modal="true"><div><StatusBadge tone="warning">Delete asset</StatusBadge><h2>Delete {pendingDelete.filename}?</h2><p>Existing presets may reference this file. They will remain saved, but cannot be applied until repaired.</p></div><div><Button variant="quiet" onClick={() => setPendingDelete(undefined)}>Cancel</Button><Button variant="danger" onClick={() => void remove()}>Delete asset</Button></div></div>}
      {conflict && <div className="confirm-popover" role="alertdialog" aria-modal="true" aria-label="Asset already exists"><div><StatusBadge tone="warning">File already exists</StatusBadge><h2>Replace {conflict.file.name}?</h2><p>The installed {conflict.kind === "models" ? "NAM model" : "cabinet IR"} with this filename will be replaced. Presets that reference it will use the replacement.</p></div><div><Button variant="quiet" onClick={() => void resolveConflict("skip")}>Skip file</Button><Button variant="danger" onClick={() => void resolveConflict("replace")}>Replace asset</Button></div></div>}
      {renaming && <div className="confirm-popover" role="dialog" aria-modal="true" aria-label="Rename asset"><div><StatusBadge tone="info">Rename asset</StatusBadge><h2>Rename {renaming.filename}</h2><p>Use a filename ending in {kind === "models" ? ".nam" : ".wav"}. Saved presets that use this asset will be updated automatically.</p><label className="rename-field">Filename<input autoFocus aria-label="New filename" value={renameValue} onChange={(event) => setRenameValue(event.target.value)} onKeyDown={(event) => { if (event.key === "Enter") void rename(); }} /></label></div><div><Button variant="quiet" onClick={() => setRenaming(undefined)}>Cancel</Button><Button variant="primary" onClick={() => void rename()}>Rename asset</Button></div></div>}
    </main>
  );
}
