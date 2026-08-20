import { FileAudio, Music2, Pencil, Trash2, Upload } from "lucide-react";
import { Suspense, lazy, useEffect, useRef, useState } from "react";

import type { Asset, AssetKind } from "../api/types";
import { ArdorApiError } from "../api/errors";
import { Button, IconButton, StatusBadge } from "../components/ui";
import { ConfirmPopover } from "../components/ConfirmPopover";
import { useDeviceSession } from "../connection/deviceSession";
import type { Tone3000Selection } from "../tone3000/types";
import { Tone3000Brand } from "../tone3000/Tone3000Brand";
import type { Tone3000Phase } from "../tone3000/Tone3000Dialog";
import {
  getHostedTone3000Selection,
  installHostedTone3000Model,
  startHostedTone3000Selection,
  type Tone3000Architecture,
} from "../tone3000/hosted";
import { isHostedCloudRuntime } from "../runtime/platform";

const Tone3000Dialog = lazy(() => import("../tone3000/Tone3000Dialog").then((module) => ({ default: module.Tone3000Dialog })));

function size(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

export function AssetLibrary({ tone3000DeviceId }: { tone3000DeviceId?: string } = {}) {
  const session = useDeviceSession();
  const hostedCloud = isHostedCloudRuntime();
  const [kind, setKind] = useState<AssetKind>("models");
  const [query, setQuery] = useState("");
  const [error, setError] = useState<string>();
  const [notice, setNotice] = useState<string>();
  const [pendingDelete, setPendingDelete] = useState<Asset[]>();
  const [selected, setSelected] = useState<ReadonlySet<string>>(new Set());
  const [conflict, setConflict] = useState<{ file: File; kind: AssetKind; remaining: File[] }>();
  const [renaming, setRenaming] = useState<Asset>();
  const [renameValue, setRenameValue] = useState("");
  const [tone3000Phase, setTone3000Phase] = useState<Tone3000Phase>("idle");
  const [tone3000Selection, setTone3000Selection] = useState<Tone3000Selection>();
  const [selectedTone3000ModelId, setSelectedTone3000ModelId] = useState<number>();
  const [tone3000Architecture, setTone3000Architecture] = useState<Tone3000Architecture>("legacy");
  const [tone3000FlowId, setTone3000FlowId] = useState<string>();
  const fileRef = useRef<HTMLInputElement>(null);
  const hostedFlowGeneration = useRef(0);
  const hostedPopup = useRef<Window | null>(null);
  const assets = kind === "models" ? session.models : session.irs;
  const visible = assets.filter((asset) => asset.filename.toLowerCase().includes(query.toLowerCase()));
  const tone3000Available = hostedCloud && tone3000DeviceId !== undefined;
  const allVisibleSelected = visible.length > 0 && visible.every((asset) => selected.has(asset.id));

  // ponytail: any list change (tab switch, upload, rename) just drops the selection.
  useEffect(() => { setSelected(new Set()); }, [kind, assets]);

  const toggleSelected = (id: string) => setSelected((current) => {
    const next = new Set(current);
    if (!next.delete(id)) next.add(id);
    return next;
  });
  const toggleAllSelected = () => setSelected(allVisibleSelected ? new Set() : new Set(visible.map((asset) => asset.id)));

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
    setError(undefined);
    setNotice(undefined);
    const failed: string[] = [];
    for (const asset of pendingDelete) {
      try {
        await session.client.deleteAsset(kind, asset.id);
      } catch (reason) {
        failed.push(`${asset.filename} (${reason instanceof Error ? reason.message : "unknown error"})`);
      }
    }
    await session.refreshAssets(kind);
    setPendingDelete(undefined);
    setSelected(new Set());
    if (failed.length > 0) setError(`Could not delete ${failed.join(", ")}.`);
    else setNotice(pendingDelete.length === 1 ? `${pendingDelete[0].filename} deleted.` : `${pendingDelete.length} assets deleted.`);
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

  const launchTone3000 = async () => {
    setError(undefined);
    if (!tone3000DeviceId) {
      setError("No hosted device is selected.");
      return;
    }
    const generation = ++hostedFlowGeneration.current;
    const popup = window.open("", "ardor-tone3000", "popup,width=1100,height=760");
    if (!popup) {
      setError("Allow pop-ups for Ardor to browse TONE3000.");
      return;
    }
    hostedPopup.current = popup;
    try {
      setTone3000Phase("waiting");
      const started = await startHostedTone3000Selection(tone3000DeviceId, tone3000Architecture);
      if (generation !== hostedFlowGeneration.current) return;
      setTone3000FlowId(started.flowId);
      popup.location.replace(started.authorizeUrl);
      for (;;) {
        await new Promise((resolve) => window.setTimeout(resolve, 900));
        if (generation !== hostedFlowGeneration.current) return;
        const current = await getHostedTone3000Selection(started.flowId);
        if (current.status === "failed") throw new Error(current.message || "TONE3000 selection failed.");
        if (current.status === "ready" && current.selection) {
          setTone3000Selection(current.selection);
          setSelectedTone3000ModelId(current.selection.models[0]?.id);
          setTone3000Phase("detail");
          popup.close();
          hostedPopup.current = null;
          return;
        }
      }
    } catch (reason) {
      popup.close();
      hostedPopup.current = null;
      setTone3000Phase("idle");
      setError(reason instanceof Error ? reason.message : "Could not open TONE3000.");
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
    hostedFlowGeneration.current += 1;
    hostedPopup.current?.close();
    hostedPopup.current = null;
    setTone3000Phase("idle");
  };

  const installTone3000Model = async () => {
    const selection = tone3000Selection;
    const model = selection?.models.find(({ id }) => id === selectedTone3000ModelId);
    if (!selection || !model) return;
    setError(undefined);
    setTone3000Phase("installing");
    try {
      if (!tone3000FlowId) throw new Error("TONE3000 selection is no longer available.");
      await installHostedTone3000Model(tone3000FlowId, model.id);
      await session.refreshAssets("models");
      setKind("models");
      setTone3000Phase("idle");
      setTone3000FlowId(undefined);
      setNotice(`${model.name} by @${selection.tone.user.username} installed from TONE3000.`);
    } catch (reason) {
      setTone3000Phase("detail");
      setError(reason instanceof Error ? reason.message : "Could not install the Tone3000 model.");
    }
  };

  if (session.status !== "connected") return <main className="assets-view"><div className="assets-empty"><Music2 size={36} /><h1>Connect to manage assets</h1><p>Models and cabinet IRs live on the pedal and are available to preset blocks after upload.</p></div></main>;

  return (
    <main className="assets-view">
      <header className="assets-view__header"><div><p className="eyebrow">Device assets</p><h1>Models & cabinet IRs</h1><p>Upload files once, then choose them from the relevant block inspector.</p></div><div className="assets-view__actions">{kind === "models" && tone3000Available && <Button className="tone3000-entry" onClick={browseTone3000} disabled={session.busy.upload || conflict !== undefined || tone3000Phase !== "idle"}><Tone3000Brand compact /> Browse TONE3000</Button>}<Button onClick={() => fileRef.current?.click()} disabled={session.busy.upload || conflict !== undefined}><Upload size={16} /> {session.busy.upload ? "Uploading…" : "Upload files"}</Button></div><input ref={fileRef} hidden type="file" multiple accept={kind === "models" ? ".nam" : ".wav"} onChange={(event) => upload(event.target.files)} /></header>
      <div className="assets-toolbar"><div className="category-tabs" role="group" aria-label="Asset type"><button type="button" aria-pressed={kind === "models"} className={kind === "models" ? "is-active" : ""} onClick={() => setKind("models")}>NAM models</button><button type="button" aria-pressed={kind === "irs"} className={kind === "irs" ? "is-active" : ""} onClick={() => setKind("irs")}>Cabinet IRs</button></div><input className="asset-search" aria-label="Search files" placeholder="Search files" value={query} onChange={(event) => setQuery(event.target.value)} /></div>
      <div className="asset-dropzone" onDragOver={(event) => event.preventDefault()} onDrop={(event) => { event.preventDefault(); upload(event.dataTransfer.files); }}><Upload size={16} /><span>Drop {kind === "models" ? ".nam" : ".wav"} files here to upload</span></div>
      {error && <p className="assets-error" role="alert">{error}</p>}
      {notice && <p className="assets-notice" role="status">{notice}</p>}
      {visible.length > 0 && <div className="asset-bulkbar"><label><input type="checkbox" aria-label="Select all files" checked={allVisibleSelected} onChange={toggleAllSelected} /> {selected.size > 0 ? `${selected.size} selected` : "Select all"}</label>{selected.size > 0 && <Button variant="danger" onClick={() => setPendingDelete(visible.filter((asset) => selected.has(asset.id)))}><Trash2 size={15} /> Delete selected</Button>}</div>}
      <section className="asset-table" aria-label={kind === "models" ? "NAM models" : "Cabinet IRs"}>{visible.map((asset) => <article className={selected.has(asset.id) ? "asset-row is-selected" : "asset-row"} key={asset.id}><input type="checkbox" aria-label={`Select ${asset.filename}`} checked={selected.has(asset.id)} onChange={() => toggleSelected(asset.id)} /><span className="asset-row__icon">{kind === "models" ? <Music2 size={18} /> : <FileAudio size={18} />}</span><div><strong>{asset.filename}</strong><small>{asset.path}</small></div><span>{size(asset.sizeBytes)}</span><IconButton label={`Rename ${asset.filename}`} onClick={() => beginRename(asset)}><Pencil size={15} /></IconButton><IconButton label={`Delete ${asset.filename}`} onClick={() => setPendingDelete([asset])}><Trash2 size={16} /></IconButton></article>)}{visible.length === 0 && <div className="assets-empty"><FileAudio size={32} /><h2>No {kind === "models" ? "models" : "IRs"} yet</h2><p>Upload a {kind === "models" ? ".nam model" : ".wav cabinet impulse response"} to use it in a preset.</p></div>}</section>
      <ConfirmPopover
        open={pendingDelete !== undefined}
        onOpenChange={(open) => { if (!open) setPendingDelete(undefined); }}
        badge={<StatusBadge tone="warning">{pendingDelete?.length === 1 ? "Delete asset" : "Delete assets"}</StatusBadge>}
        title={<>Delete {pendingDelete?.length === 1 ? pendingDelete[0].filename : `${pendingDelete?.length ?? 0} files`}?</>}
        role="alertdialog"
        actions={<><Button variant="quiet" onClick={() => setPendingDelete(undefined)}>Cancel</Button><Button variant="danger" onClick={() => void remove()}>{pendingDelete?.length === 1 ? "Delete asset" : `Delete ${pendingDelete?.length ?? 0} assets`}</Button></>}
      ><p>Existing presets may reference {pendingDelete?.length === 1 ? "this file" : "these files"}. They will remain saved, but cannot be applied until repaired.</p></ConfirmPopover>
      <ConfirmPopover
        open={conflict !== undefined}
        onOpenChange={(open) => { if (!open) void resolveConflict("skip"); }}
        badge={<StatusBadge tone="warning">File already exists</StatusBadge>}
        title={<>Replace {conflict?.file.name}?</>}
        role="alertdialog"
        actions={<><Button variant="quiet" onClick={() => void resolveConflict("skip")}>Skip file</Button><Button variant="danger" onClick={() => void resolveConflict("replace")}>Replace asset</Button></>}
      ><p>The installed {conflict?.kind === "models" ? "NAM model" : "cabinet IR"} with this filename will be replaced. Presets that reference it will use the replacement.</p></ConfirmPopover>
      <ConfirmPopover
        open={renaming !== undefined}
        onOpenChange={(open) => { if (!open) setRenaming(undefined); }}
        badge={<StatusBadge tone="info">Rename asset</StatusBadge>}
        title={<>Rename {renaming?.filename}</>}
        actions={<><Button variant="quiet" onClick={() => setRenaming(undefined)}>Cancel</Button><Button variant="primary" onClick={() => void rename()}>Rename asset</Button></>}
      >
        <p>Use a filename ending in {kind === "models" ? ".nam" : ".wav"}. Saved presets that use this asset will be updated automatically.</p>
        <label className="rename-field">Filename<input aria-label="New filename" value={renameValue} onChange={(event) => setRenameValue(event.target.value)} onKeyDown={(event) => { if (event.key === "Enter") void rename(); }} /></label>
      </ConfirmPopover>
      {tone3000Phase !== "idle" && <Suspense fallback={null}>
        <Tone3000Dialog phase={tone3000Phase} selection={tone3000Selection} selectedModelId={selectedTone3000ModelId} onSelectedModelId={setSelectedTone3000ModelId} onContinue={continueToTone3000} onCancel={cancelTone3000Flow} onInstall={() => void installTone3000Model()} architecture={tone3000Architecture} onArchitecture={setTone3000Architecture} />
      </Suspense>}
    </main>
  );
}
