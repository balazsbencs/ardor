import { FileAudio, Music2, Pencil, Trash2, Upload } from "lucide-react";
import { useCallback, useEffect, useRef, useState } from "react";

import type { Asset, AssetKind } from "../api/types";
import { ArdorApiError } from "../api/errors";
import { Button, IconButton, StatusBadge } from "../components/ui";
import { useDeviceSession } from "../connection/deviceSession";
import {
  completeTone3000Selection,
  createTone3000SelectUrl,
  downloadTone3000Model,
  TONE3000_REDIRECT_URI,
  tone3000Configured,
  type Tone3000Selection,
} from "../tone3000/client";
import { cancelTone3000, onTone3000Callback, openTone3000, tone3000NativeAvailable } from "../tone3000/native";
import { Tone3000Brand, Tone3000Dialog, type Tone3000Phase } from "../tone3000/Tone3000Dialog";

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
  const [tone3000Phase, setTone3000Phase] = useState<Tone3000Phase>("idle");
  const [tone3000Selection, setTone3000Selection] = useState<Tone3000Selection>();
  const [selectedTone3000ModelId, setSelectedTone3000ModelId] = useState<number>();
  const fileRef = useRef<HTMLInputElement>(null);
  const processedDeepLinks = useRef(new Set<string>());
  const assets = kind === "models" ? session.models : session.irs;
  const visible = assets.filter((asset) => asset.filename.toLowerCase().includes(query.toLowerCase()));

  const clearFileInput = () => { if (fileRef.current) fileRef.current.value = ""; };
  const uploadSequentially = async (uploadKind: AssetKind, files: File[]): Promise<boolean> => {
    if (files.length === 0) {
      clearFileInput();
      return true;
    }
    let succeeded = true;
    setError(undefined);
    setNotice(undefined);
    for (const [index, file] of files.entries()) {
      const expected = uploadKind === "models" ? ".nam" : ".wav";
      if (!file.name.toLowerCase().endsWith(expected)) {
        setError(`${file.name} is not a ${expected} file.`);
        succeeded = false;
        continue;
      }
      try {
        const response = await session.uploadAsset(uploadKind, file, false);
        if (response) {
          await session.refreshAssets(uploadKind);
          setNotice(`${file.name} uploaded.`);
        } else {
          succeeded = false;
        }
      } catch (reason) {
        if (reason instanceof ArdorApiError && reason.code === "asset_exists") {
          setConflict({ file, kind: uploadKind, remaining: files.slice(index + 1) });
          return false;
        }
        setError(reason instanceof Error ? reason.message : `Could not upload ${file.name}.`);
        succeeded = false;
      }
    }
    clearFileInput();
    return succeeded;
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

  const processTone3000Callback = useCallback(async (url: string) => {
    if (!url.startsWith(TONE3000_REDIRECT_URI) || processedDeepLinks.current.has(url)) return;
    processedDeepLinks.current.add(url);
    setTone3000Phase("loading");
    setError(undefined);
    try {
      const selection = await completeTone3000Selection(url);
      setTone3000Selection(selection);
      setSelectedTone3000ModelId(selection.models[0]?.id);
      setTone3000Phase("detail");
    } catch (reason) {
      setTone3000Phase("idle");
      setError(reason instanceof Error ? reason.message : "Could not load the Tone3000 selection.");
    }
  }, []);

  useEffect(() => {
    let disposed = false;
    let unlisten: () => void = () => undefined;
    void onTone3000Callback((url) => void processTone3000Callback(url))
      .then((nextUnlisten) => { if (disposed) nextUnlisten(); else unlisten = nextUnlisten; });
    return () => { disposed = true; unlisten(); };
  }, [processTone3000Callback]);

  const launchTone3000 = async () => {
    setError(undefined);
    if (!tone3000Configured()) {
      setError("Tone3000 is not configured for this build.");
      return;
    }
    if (!tone3000NativeAvailable()) {
      setError("Tone3000 browsing is available in the Ardor desktop app.");
      return;
    }
    try {
      setTone3000Phase("waiting");
      const url = await createTone3000SelectUrl();
      await openTone3000(url);
    } catch (reason) {
      setTone3000Phase("idle");
      setError(reason instanceof Error ? reason.message : "Could not open Tone3000.");
    }
  };

  const browseTone3000 = () => {
    if (sessionStorage.getItem("ardor-manager.tone3000.introduced") === "1") {
      void launchTone3000();
    } else {
      setTone3000Phase("intro");
    }
  };

  const continueToTone3000 = () => {
    sessionStorage.setItem("ardor-manager.tone3000.introduced", "1");
    void launchTone3000();
  };

  const cancelTone3000Flow = () => {
    setTone3000Phase("idle");
    void cancelTone3000();
  };

  const installTone3000Model = async () => {
    const selection = tone3000Selection;
    const model = selection?.models.find(({ id }) => id === selectedTone3000ModelId);
    if (!selection || !model) return;
    setError(undefined);
    setTone3000Phase("installing");
    try {
      const file = await downloadTone3000Model(selection, model);
      setKind("models");
      const uploaded = await uploadSequentially("models", [file]);
      setTone3000Phase("idle");
      if (uploaded) setNotice(`${model.name} by @${selection.tone.user.username} installed from TONE3000.`);
    } catch (reason) {
      setTone3000Phase("detail");
      setError(reason instanceof Error ? reason.message : "Could not install the Tone3000 model.");
    }
  };

  if (session.status !== "connected") return <main className="assets-view"><div className="assets-empty"><Music2 size={36} /><h1>Connect to manage assets</h1><p>Models and cabinet IRs live on the pedal and are available to preset blocks after upload.</p></div></main>;

  return (
    <main className="assets-view">
      <header className="assets-view__header"><div><p className="eyebrow">Device assets</p><h1>Models & cabinet IRs</h1><p>Upload files once, then choose them from the relevant block inspector.</p></div><div className="assets-view__actions">{kind === "models" && <Button className="tone3000-entry" onClick={browseTone3000} disabled={session.busy.upload || conflict !== undefined || tone3000Phase !== "idle"}><Tone3000Brand compact /> Browse TONE3000</Button>}<Button onClick={() => fileRef.current?.click()} disabled={session.busy.upload || conflict !== undefined}><Upload size={16} /> {session.busy.upload ? "Uploading…" : "Upload files"}</Button></div><input ref={fileRef} hidden type="file" multiple accept={kind === "models" ? ".nam" : ".wav"} onChange={(event) => upload(event.target.files)} /></header>
      <div className="assets-toolbar"><div className="category-tabs" role="tablist" aria-label="Asset type"><button role="tab" aria-selected={kind === "models"} className={kind === "models" ? "is-active" : ""} onClick={() => setKind("models")}>NAM models</button><button role="tab" aria-selected={kind === "irs"} className={kind === "irs" ? "is-active" : ""} onClick={() => setKind("irs")}>Cabinet IRs</button></div><input className="asset-search" placeholder="Search files" value={query} onChange={(event) => setQuery(event.target.value)} /></div>
      <div className="asset-dropzone" onDragOver={(event) => event.preventDefault()} onDrop={(event) => { event.preventDefault(); upload(event.dataTransfer.files); }}><Upload size={16} /><span>Drop {kind === "models" ? ".nam" : ".wav"} files here to upload</span></div>
      {error && <p className="assets-error" role="alert">{error}</p>}
      {notice && <p className="assets-notice" role="status">{notice}</p>}
      <section className="asset-table" aria-label={kind === "models" ? "NAM models" : "Cabinet IRs"}>{visible.map((asset) => <article className="asset-row" key={asset.id}><span className="asset-row__icon">{kind === "models" ? <Music2 size={18} /> : <FileAudio size={18} />}</span><div><strong>{asset.filename}</strong><small>{asset.path}</small></div><span>{size(asset.sizeBytes)}</span><IconButton label={`Rename ${asset.filename}`} onClick={() => beginRename(asset)}><Pencil size={15} /></IconButton><IconButton label={`Delete ${asset.filename}`} onClick={() => setPendingDelete(asset)}><Trash2 size={16} /></IconButton></article>)}{visible.length === 0 && <div className="assets-empty"><FileAudio size={32} /><h2>No {kind === "models" ? "models" : "IRs"} yet</h2><p>Upload a {kind === "models" ? ".nam model" : ".wav cabinet impulse response"} to use it in a preset.</p></div>}</section>
      {pendingDelete && <div className="confirm-popover" role="alertdialog" aria-modal="true"><div><StatusBadge tone="warning">Delete asset</StatusBadge><h2>Delete {pendingDelete.filename}?</h2><p>Existing presets may reference this file. They will remain saved, but cannot be applied until repaired.</p></div><div><Button variant="quiet" onClick={() => setPendingDelete(undefined)}>Cancel</Button><Button variant="danger" onClick={() => void remove()}>Delete asset</Button></div></div>}
      {conflict && <div className="confirm-popover" role="alertdialog" aria-modal="true" aria-label="Asset already exists"><div><StatusBadge tone="warning">File already exists</StatusBadge><h2>Replace {conflict.file.name}?</h2><p>The installed {conflict.kind === "models" ? "NAM model" : "cabinet IR"} with this filename will be replaced. Presets that reference it will use the replacement.</p></div><div><Button variant="quiet" onClick={() => void resolveConflict("skip")}>Skip file</Button><Button variant="danger" onClick={() => void resolveConflict("replace")}>Replace asset</Button></div></div>}
      {renaming && <div className="confirm-popover" role="dialog" aria-modal="true" aria-label="Rename asset"><div><StatusBadge tone="info">Rename asset</StatusBadge><h2>Rename {renaming.filename}</h2><p>Use a filename ending in {kind === "models" ? ".nam" : ".wav"}. Saved presets that use this asset will be updated automatically.</p><label className="rename-field">Filename<input autoFocus aria-label="New filename" value={renameValue} onChange={(event) => setRenameValue(event.target.value)} onKeyDown={(event) => { if (event.key === "Enter") void rename(); }} /></label></div><div><Button variant="quiet" onClick={() => setRenaming(undefined)}>Cancel</Button><Button variant="primary" onClick={() => void rename()}>Rename asset</Button></div></div>}
      <Tone3000Dialog phase={tone3000Phase} selection={tone3000Selection} selectedModelId={selectedTone3000ModelId} onSelectedModelId={setSelectedTone3000ModelId} onContinue={continueToTone3000} onCancel={cancelTone3000Flow} onInstall={() => void installTone3000Model()} />
    </main>
  );
}
