import { createContext, useContext, useEffect, useReducer, useRef, useState, type Dispatch, type PropsWithChildren } from "react";

import type { AssetKind } from "../api/types";
import { useDeviceSession } from "../connection/deviceSession";
import { KIND_LABELS } from "./libraryView";
import { nextUpload, uploadQueue, type QueueAction, type QueueItem } from "./uploadQueue";

export const failureReason = (failure: unknown, fallback: string) => (failure instanceof Error ? failure.message : fallback);

export type AssetQueueValue = {
  queue: QueueItem[];
  dispatchQueue: Dispatch<QueueAction>;
  notice?: string;
  error?: string;
  setNotice(message?: string): void;
  setError(message?: string): void;
  /** Refreshes one kind and the usage. A failure is reported on its own and never undoes the notice. */
  refreshLists(kind: AssetKind, fallback: string, keep?: string): Promise<void>;
};

const AssetQueueContext = createContext<AssetQueueValue | undefined>(undefined);

/**
 * The upload queue, its runner and the library notices. It sits above the views, so a batch keeps
 * uploading and parked conflicts stay when the person goes to Edit and back.
 */
export function AssetQueueProvider({ children }: PropsWithChildren) {
  const session = useDeviceSession();
  const latest = useRef(session);
  latest.current = session;
  const [queue, dispatchQueue] = useReducer(uploadQueue, []);
  const [notice, setNotice] = useState<string>();
  const [error, setError] = useState<string>();

  const refreshLists = async (kind: AssetKind, fallback: string, keep?: string) => {
    try {
      await latest.current.refreshAssets(kind);
      await latest.current.refreshAssetUsage?.();
    } catch (failure) {
      const message = failureReason(failure, fallback);
      setError(keep ? `${keep} ${message}` : message);
    }
  };

  // Uploads one waiting item at a time. Conflicts stay parked until the person resolves them.
  useEffect(() => {
    if (queue.some(({ state }) => state === "uploading")) return;
    const item = nextUpload(queue);
    if (!item) return;
    dispatchQueue({ type: "start", id: item.id });
    void (async () => {
      try {
        const uploaded = await latest.current.uploadAsset(item.kind, item.file, item.replace);
        if (!uploaded) throw new Error(`Could not upload ${item.file.name}.`);
        dispatchQueue({ type: "done", id: item.id });
        setNotice(`${item.file.name} uploaded to ${KIND_LABELS[item.kind]}.`);
        await refreshLists(item.kind, `${item.file.name} uploaded, but the file list did not refresh.`);
      } catch (failure) {
        dispatchQueue({ type: "failed", id: item.id });
        setError(failureReason(failure, `Could not upload ${item.file.name}.`));
      }
    })();
  }, [queue]);

  const value: AssetQueueValue = { queue, dispatchQueue, notice, error, setNotice, setError, refreshLists };
  return <AssetQueueContext.Provider value={value}>{children}</AssetQueueContext.Provider>;
}

export function useAssetQueue(): AssetQueueValue {
  const value = useContext(AssetQueueContext);
  if (!value) throw new Error("useAssetQueue must be used within AssetQueueProvider");
  return value;
}
