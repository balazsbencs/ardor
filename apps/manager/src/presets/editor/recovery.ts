import type { Preset } from "../../api/types";
import { deepEqual } from "./editorReducer";
import type { PresetLocation } from "./editorTypes";

export type RecoveryRecord = {
  draft: Preset;
  savedSnapshot: string;
  timestamp: number;
};

export type RecoveryReadResult =
  | { status: "none" }
  | { status: "available" | "stale"; record: RecoveryRecord };

export function recoveryKey(baseUrl: string, location: PresetLocation): string {
  return `ardor-manager.recovery:${encodeURIComponent(baseUrl)}:${location.bank}:${location.slot}`;
}

export function writeRecovery(
  storage: Storage,
  baseUrl: string,
  location: PresetLocation,
  draft: Preset,
  saved: Preset,
  timestamp = Date.now(),
): void {
  const key = recoveryKey(baseUrl, location);
  if (deepEqual(draft, saved)) {
    storage.removeItem(key);
    return;
  }
  const record: RecoveryRecord = {
    draft: structuredClone(draft),
    savedSnapshot: JSON.stringify(saved),
    timestamp,
  };
  storage.setItem(key, JSON.stringify(record));
}

export function readRecovery(
  storage: Storage,
  baseUrl: string,
  location: PresetLocation,
  freshlyLoadedSaved: Preset,
): RecoveryReadResult {
  const raw = storage.getItem(recoveryKey(baseUrl, location));
  if (!raw) return { status: "none" };
  try {
    const record = JSON.parse(raw) as RecoveryRecord;
    if (!record || typeof record.timestamp !== "number" || typeof record.savedSnapshot !== "string" || !record.draft) {
      return { status: "none" };
    }
    const basis = JSON.parse(record.savedSnapshot) as Preset;
    if (!deepEqual(basis, freshlyLoadedSaved)) return { status: "stale", record };
    if (deepEqual(record.draft, freshlyLoadedSaved)) return { status: "none" };
    return { status: "available", record };
  } catch {
    return { status: "none" };
  }
}

export function removeRecovery(storage: Storage, baseUrl: string, location: PresetLocation): void {
  storage.removeItem(recoveryKey(baseUrl, location));
}

export function createDebouncedRecoveryWriter(storage: Storage, delayMs = 500) {
  let timer: ReturnType<typeof setTimeout> | undefined;
  return {
    schedule(baseUrl: string, location: PresetLocation, draft: Preset, saved: Preset) {
      if (timer !== undefined) clearTimeout(timer);
      const draftSnapshot = structuredClone(draft);
      const savedSnapshot = structuredClone(saved);
      timer = setTimeout(() => {
        timer = undefined;
        writeRecovery(storage, baseUrl, location, draftSnapshot, savedSnapshot);
      }, delayMs);
    },
    cancel() {
      if (timer !== undefined) clearTimeout(timer);
      timer = undefined;
    },
  };
}

export type UnsavedChoice = "save" | "discard" | "cancel";
export type NavigationRequestResult = "continued" | "prompt";

type NavigationControllerOptions = {
  isDirty(): boolean;
  save(): Promise<boolean>;
  discard(): void;
};

export function createUnsavedNavigationController(options: NavigationControllerOptions) {
  let pending: (() => void | Promise<void>) | undefined;
  return {
    get hasPendingNavigation() { return pending !== undefined; },
    async request(navigate: () => void | Promise<void>): Promise<NavigationRequestResult> {
      if (!options.isDirty()) {
        await navigate();
        return "continued";
      }
      if (!pending) pending = navigate;
      return "prompt";
    },
    async resolve(choice: UnsavedChoice): Promise<boolean> {
      if (!pending) return false;
      if (choice === "cancel") {
        pending = undefined;
        return false;
      }
      if (choice === "save" && !await options.save()) return false;
      if (choice === "discard") options.discard();
      const navigate = pending;
      pending = undefined;
      await navigate();
      return true;
    },
  };
}
