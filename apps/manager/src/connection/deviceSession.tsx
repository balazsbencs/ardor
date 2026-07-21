import {
  createContext,
  type PropsWithChildren,
  useContext,
  useMemo,
  useRef,
  useState,
} from "react";

import { ArdorApiClient, type ApiClientConfig } from "../api/client";
import { ArdorApiError } from "../api/errors";
import type {
  ApplyPresetResponse,
  Asset,
  AssetKind,
  DeviceStatus,
  Preset,
  PresetSlot,
  PresetSlotSummary,
} from "../api/types";
import type { PresetLocation } from "../presets/editor/editorTypes";
import { createEmptyPreset } from "../presets/editor/presetFactory";

export type SessionStatus = "disconnected" | "connecting" | "connected" | "error";

export type SessionPreset = {
  location: PresetLocation;
  preset: Preset;
  exists: boolean;
};

export type DeviceClientFactory = (config: ApiClientConfig) => ArdorApiClient;

type BusyState = { save: boolean; apply: boolean; upload: boolean };

export type DeviceSessionValue = {
  status: SessionStatus;
  baseUrl: string;
  device?: DeviceStatus;
  client?: ArdorApiClient;
  models: Asset[];
  irs: Asset[];
  presets: PresetSlotSummary[];
  current?: SessionPreset;
  error?: Error;
  needsTokenFocus: boolean;
  busy: BusyState;
  connect(baseUrl: string, token?: string): Promise<void>;
  disconnect(): void;
  selectLocation(location: PresetLocation): Promise<void>;
  refreshAssets(kind?: AssetKind): Promise<void>;
  refreshPresets(): Promise<void>;
  saveCurrent(preset: Preset): Promise<PresetSlot | undefined>;
  applyCurrent(): Promise<ApplyPresetResponse | undefined>;
  uploadAsset(kind: AssetKind, file: File, overwrite: boolean): Promise<Asset | undefined>;
};

const baseUrlKey = "ardor-manager.base-url";
const locationKey = (baseUrl: string) => `ardor-manager.location:${baseUrl}`;
const defaultBaseUrl = "http://127.0.0.1:8080";
const defaultBusy: BusyState = { save: false, apply: false, upload: false };

export const DeviceSessionContext = createContext<DeviceSessionValue | undefined>(undefined);

function storedLocation(baseUrl: string): PresetLocation | undefined {
  try {
    const value = JSON.parse(localStorage.getItem(locationKey(baseUrl)) ?? "null") as unknown;
    if (typeof value !== "object" || value === null) return undefined;
    const location = value as Record<string, unknown>;
    return Number.isInteger(location.bank) && Number.isInteger(location.slot)
      && Number(location.bank) >= 0 && Number(location.bank) <= 99
      && Number(location.slot) >= 0 && Number(location.slot) <= 3
      ? { bank: Number(location.bank), slot: Number(location.slot) }
      : undefined;
  } catch {
    return undefined;
  }
}

function hasPreset(summaries: PresetSlotSummary[], location: PresetLocation): boolean {
  return summaries.some(({ bank, slot, exists }) => bank === location.bank && slot === location.slot && exists);
}

async function loadInitialPreset(
  client: ArdorApiClient,
  baseUrl: string,
  device: DeviceStatus,
  summaries: PresetSlotSummary[],
): Promise<SessionPreset> {
  if (device.active && hasPreset(summaries, device.active)) {
    try {
      const response = await client.getPreset(device.active.bank, device.active.slot);
      return { location: { bank: response.bank, slot: response.slot }, preset: response.preset, exists: true };
    } catch {
      // A stale optional active location falls through to the local selection.
    }
  }
  const location = storedLocation(baseUrl) ?? { bank: 0, slot: 0 };
  if (!hasPreset(summaries, location)) {
    return { location, preset: createEmptyPreset(), exists: false };
  }
  try {
    const response = await client.getPreset(location.bank, location.slot);
    return { location, preset: response.preset, exists: true };
  } catch {
    const fallback = { bank: 0, slot: 0 };
    if (hasPreset(summaries, fallback)) {
      const response = await client.getPreset(fallback.bank, fallback.slot);
      return { location: fallback, preset: response.preset, exists: true };
    }
    return { location: fallback, preset: createEmptyPreset(), exists: false };
  }
}

export function DeviceSessionProvider({
  children,
  clientFactory = (config) => new ArdorApiClient(config),
}: PropsWithChildren<{ clientFactory?: DeviceClientFactory }>) {
  const [status, setStatus] = useState<SessionStatus>("disconnected");
  const [baseUrl, setBaseUrl] = useState(() => localStorage.getItem(baseUrlKey) ?? defaultBaseUrl);
  const [device, setDevice] = useState<DeviceStatus>();
  const [client, setClient] = useState<ArdorApiClient>();
  const [models, setModels] = useState<Asset[]>([]);
  const [irs, setIrs] = useState<Asset[]>([]);
  const [presets, setPresets] = useState<PresetSlotSummary[]>([]);
  const [current, setCurrent] = useState<SessionPreset>();
  const [error, setError] = useState<Error>();
  const [needsTokenFocus, setNeedsTokenFocus] = useState(false);
  const [busy, setBusy] = useState(defaultBusy);
  const connecting = useRef(false);
  const operationBusy = useRef(defaultBusy);

  const setOperationBusy = (kind: keyof BusyState, value: boolean) => {
    operationBusy.current = { ...operationBusy.current, [kind]: value };
    setBusy(operationBusy.current);
  };

  const connect = async (nextBaseUrl: string, token?: string) => {
    if (connecting.current) return;
    connecting.current = true;
    setStatus("connecting");
    setError(undefined);
    setNeedsTokenFocus(false);
    const normalizedBaseUrl = nextBaseUrl.replace(/\/+$/, "");
    const nextClient = clientFactory({ baseUrl: normalizedBaseUrl, token: token || undefined });
    try {
      const nextDevice = await nextClient.getDevice();
      const [nextModels, nextIrs, nextPresets] = await Promise.all([
        nextClient.listAssets("models"),
        nextClient.listAssets("irs"),
        nextClient.listPresets(),
      ]);
      const nextCurrent = await loadInitialPreset(nextClient, normalizedBaseUrl, nextDevice, nextPresets);
      localStorage.setItem(baseUrlKey, normalizedBaseUrl);
      localStorage.setItem(locationKey(normalizedBaseUrl), JSON.stringify(nextCurrent.location));
      setBaseUrl(normalizedBaseUrl);
      setClient(nextClient);
      setDevice(nextDevice);
      setModels(nextModels);
      setIrs(nextIrs);
      setPresets(nextPresets);
      setCurrent(nextCurrent);
      setStatus("connected");
    } catch (reason) {
      const nextError = reason instanceof Error ? reason : new Error("Connection failed");
      setClient(undefined);
      setDevice(undefined);
      setCurrent(undefined);
      setStatus("error");
      setError(nextError);
      setNeedsTokenFocus(nextError instanceof ArdorApiError && nextError.status === 401);
    } finally {
      connecting.current = false;
    }
  };

  const disconnect = () => {
    setStatus("disconnected");
    setClient(undefined);
    setDevice(undefined);
    setModels([]);
    setIrs([]);
    setPresets([]);
    setCurrent(undefined);
    setError(undefined);
    setNeedsTokenFocus(false);
  };

  const selectLocation = async (location: PresetLocation) => {
    if (!client || status !== "connected") return;
    let next: SessionPreset;
    if (hasPreset(presets, location)) {
      const response = await client.getPreset(location.bank, location.slot);
      next = { location, preset: response.preset, exists: true };
    } else {
      next = { location, preset: createEmptyPreset(), exists: false };
    }
    localStorage.setItem(locationKey(baseUrl), JSON.stringify(location));
    setCurrent(next);
  };

  const refreshAssets = async (kind?: AssetKind) => {
    if (!client) return;
    if (!kind || kind === "models") setModels(await client.listAssets("models"));
    if (!kind || kind === "irs") setIrs(await client.listAssets("irs"));
  };

  const refreshPresets = async () => {
    if (client) setPresets(await client.listPresets());
  };

  const saveCurrent = async (preset: Preset) => {
    if (!client || !current || operationBusy.current.save) return undefined;
    setOperationBusy("save", true);
    try {
      const response = await client.savePreset(current.location.bank, current.location.slot, preset);
      setCurrent({ location: current.location, preset: response.preset, exists: true });
      return response;
    } finally {
      setOperationBusy("save", false);
    }
  };

  const applyCurrent = async () => {
    if (!client || !current || operationBusy.current.apply) return undefined;
    setOperationBusy("apply", true);
    try {
      return await client.applyPreset(current.location.bank, current.location.slot);
    } finally {
      setOperationBusy("apply", false);
    }
  };

  const uploadAsset = async (kind: AssetKind, file: File, overwrite: boolean) => {
    if (!client || operationBusy.current.upload) return undefined;
    setOperationBusy("upload", true);
    try {
      return await client.uploadAsset(kind, file, overwrite);
    } finally {
      setOperationBusy("upload", false);
    }
  };

  const value = useMemo<DeviceSessionValue>(() => ({
    status, baseUrl, device, client, models, irs, presets, current, error, needsTokenFocus, busy,
    connect, disconnect, selectLocation, refreshAssets, refreshPresets, saveCurrent, applyCurrent, uploadAsset,
  }), [status, baseUrl, device, client, models, irs, presets, current, error, needsTokenFocus, busy]);

  return <DeviceSessionContext.Provider value={value}>{children}</DeviceSessionContext.Provider>;
}

export function useDeviceSession(): DeviceSessionValue {
  const session = useContext(DeviceSessionContext);
  if (!session) throw new Error("useDeviceSession must be used within DeviceSessionProvider");
  return session;
}
