import { useEffect, useReducer, useRef, useState } from "react";

import { ArdorApiError } from "../api/errors";
import type { Asset, AssetKind } from "../api/types";
import { useDeviceSession } from "../connection/deviceSession";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { missingFiles } from "./assetUsage";
import { KIND_EXTENSIONS, KIND_LABELS, visibleAssets, type AssetSort } from "./libraryView";
import { renameDraftActions } from "./renameDraft";
import { nextUpload, routeKind, uploadQueue } from "./uploadQueue";

const reason = (failure: unknown, fallback: string) => (failure instanceof Error ? failure.message : fallback);

export function useAssetLibrary(initialKind: AssetKind = "models") {
  const session = useDeviceSession();
  const editor = usePresetEditorContext();
  const latest = useRef({ session, editor });
  latest.current = { session, editor };

  const [kind, setKindState] = useState<AssetKind>(initialKind);
  const [query, setQuery] = useState("");
  const [sort, setSort] = useState<AssetSort>("name");
  const [checked, setChecked] = useState<Set<string>>(new Set());
  const [openId, setOpenId] = useState<string>();
  const [queue, dispatchQueue] = useReducer(uploadQueue, []);
  const [confirmDelete, setConfirmDelete] = useState(false);
  const [notice, setNotice] = useState<string>();
  const [error, setError] = useState<string>();

  const inventory = { models: session.models, irs: session.irs, reverbIrs: session.reverbIrs };
  const files = kind === "models" ? session.models : kind === "irs" ? session.irs : session.reverbIrs;
  const visible = visibleAssets(files, query, sort, session.assetUsage);

  const setKind = (next: AssetKind) => {
    setKindState(next);
    setChecked(new Set());
    setConfirmDelete(false);
    setOpenId(undefined);
  };

  // Any change to the selection withdraws a pending delete question, so the person never confirms a different set.
  const toggleChecked = (id: string) => {
    setConfirmDelete(false);
    setChecked((current) => {
      const next = new Set(current);
      if (!next.delete(id)) next.add(id);
      return next;
    });
  };
  const toggleAll = () => {
    setConfirmDelete(false);
    setChecked((current) =>
      visible.length > 0 && visible.every(({ id }) => current.has(id)) ? new Set() : new Set(visible.map(({ id }) => id)));
  };
  const clearChecked = () => {
    setConfirmDelete(false);
    setChecked(new Set());
  };
  const announce = (message: string) => {
    setError(undefined);
    setNotice(message);
  };

  const enqueue = (incoming: File[], replace = false) => {
    if (incoming.length === 0) return;
    setError(undefined);
    setNotice(undefined);
    const names = (list: Asset[]) => list.map(({ filename }) => filename);
    dispatchQueue({
      type: "enqueue", files: incoming, openKind: kind, replace,
      existing: { models: names(session.models), irs: names(session.irs), "reverb-irs": names(session.reverbIrs) },
    });
    setOpenId(undefined);
    const routed = incoming.map(({ name }) => routeKind(name, kind)).find((value) => value !== undefined);
    if (routed && routed !== kind) setKind(routed);
  };

  // Uploads one waiting item at a time. Conflicts stay parked until the person resolves them.
  useEffect(() => {
    if (queue.some(({ state }) => state === "uploading")) return;
    const item = nextUpload(queue);
    if (!item) return;
    dispatchQueue({ type: "start", id: item.id });
    void (async () => {
      const { session: live } = latest.current;
      try {
        const uploaded = await live.uploadAsset(item.kind, item.file, item.replace);
        if (!uploaded) throw new Error(`Could not upload ${item.file.name}.`);
        dispatchQueue({ type: "done", id: item.id });
        setNotice(`${item.file.name} uploaded to ${KIND_LABELS[item.kind]}.`);
        await refreshLists(item.kind, `${item.file.name} uploaded, but the file list did not refresh.`);
      } catch (failure) {
        dispatchQueue({ type: "failed", id: item.id });
        setError(reason(failure, `Could not upload ${item.file.name}.`));
      }
    })();
  }, [queue]);

  // The server change already happened. A refresh failure is reported on its own and never undoes the notice.
  const refreshLists = async (refreshKind: AssetKind, fallback: string, keep?: string) => {
    const { session: live } = latest.current;
    try {
      await live.refreshAssets(refreshKind);
      await live.refreshAssetUsage?.();
    } catch (failure) {
      const message = reason(failure, fallback);
      setError(keep ? `${keep} ${message}` : message);
    }
  };

  const replaceFile = (asset: Asset, file: File) => enqueue([new File([file], asset.filename)], true);

  const followRenameInDraft = (asset: Asset, newPath: string) => {
    const { session: live, editor: ed } = latest.current;
    if (!ed.dirty) {
      if (live.current) live.selectLocation(live.current.location).catch((failure) => setError(reason(failure, "Could not reload the preset.")));
      return;
    }
    renameDraftActions(ed.present, ed.allBlocks, asset.path, newPath).forEach((action) => ed.dispatch(action));
  };

  const rename = async (asset: Asset, filename: string): Promise<string | undefined> => {
    const name = filename.trim();
    const extension = KIND_EXTENSIONS[kind];
    if (!name) return "Type a file name.";
    if (!name.toLowerCase().endsWith(extension)) return `The name must end in ${extension}.`;
    if (!session.client) return "Connect to the pedal to rename files.";
    try {
      const response = await session.client.renameAsset(kind, asset.id, name);
      followRenameInDraft(asset, response.asset.path);
      const count = response.updatedPresetCount;
      setError(undefined);
      setNotice(`Renamed to ${response.asset.filename}.${count > 0 ? ` ${count} saved preset${count === 1 ? "" : "s"} ${count === 1 ? "uses" : "use"} the new name.` : ""}`);
      setChecked(new Set());
      const renamedId = response.asset.id ?? response.asset.filename;
      setOpenId((current) => (current === asset.id ? renamedId : current));
    } catch (failure) {
      if (failure instanceof ArdorApiError && failure.code === "asset_exists") return "A file with that name is already on the pedal.";
      return reason(failure, `Could not rename ${asset.filename}.`);
    }
    await refreshLists(kind, `Renamed ${asset.filename}, but the file list did not refresh.`);
    return undefined;
  };

  const askDelete = (ids?: string[]) => {
    if (ids) setChecked(new Set(ids));
    setConfirmDelete(true);
  };
  const cancelDelete = () => setConfirmDelete(false);

  const deleteChecked = async () => {
    const targets = files.filter(({ id }) => checked.has(id));
    setConfirmDelete(false);
    if (!session.client || targets.length === 0) return;
    setError(undefined);
    setNotice(undefined);
    const failed: string[] = [];
    const deletedIds = new Set<string>();
    for (const asset of targets) {
      try {
        await session.client.deleteAsset(kind, asset.id);
        deletedIds.add(asset.id);
      } catch {
        failed.push(asset.filename);
      }
    }
    setOpenId((current) => (current !== undefined && deletedIds.has(current) ? undefined : current));
    setChecked(new Set());
    const partial = failed.length > 0 ? `Could not delete ${failed.join(", ")}.` : undefined;
    if (partial) setError(partial);
    else setNotice(`${targets.length === 1 ? targets[0].filename : `${targets.length} files`} deleted from the pedal.`);
    await refreshLists(kind, "The file list did not refresh.", partial);
  };

  return {
    kind, setKind, query, setQuery, sort, setSort, visible, files, checked, toggleChecked, toggleAll, clearChecked, announce,
    openId, setOpenId, queue,
    enqueue: (incoming: File[]) => enqueue(incoming),
    resolve: (id: number, choice: "replace" | "skip") => dispatchQueue({ type: "resolve", id, choice }),
    dismiss: (id: number) => dispatchQueue({ type: "dismiss", id }),
    replaceFile, rename, confirmDelete, askDelete, cancelDelete, deleteChecked,
    notice, error, usage: session.assetUsage, missing: missingFiles(session.assetUsage, inventory),
  };
}

export type AssetLibrary = ReturnType<typeof useAssetLibrary>;
