import { useMemo, useState } from "react";
import { ArdorApiClient } from "./api/client";
import type { Asset, AssetKind, Preset, PresetBlock, PresetSlotSummary } from "./api/types";
import {
  isKnownEditableBlock,
  setBlockAsset,
  setBlockParam,
  setGlobalParam,
  setPresetName,
} from "./presets/presetDraft";

const emptyPreset: Preset = {
  version: 1,
  name: "New Preset",
  routing: "serial",
  global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [],
};

const effectParams: Record<string, string[]> = {
  mod: ["speed", "depth", "mix", "tone", "p1", "p2", "level"],
  vintage_trem: ["speed", "depth", "mix", "tone", "p1", "p2", "level"],
  delay: ["time", "repeats", "mix", "filter", "grit", "mod_spd", "mod_dep"],
  digital: ["time", "repeats", "mix", "filter", "grit", "mod_spd", "mod_dep"],
  reverb: ["decay", "pre_delay", "mix", "tone", "mod", "param1", "param2"],
  room: ["decay", "pre_delay", "mix", "tone", "mod", "param1", "param2"],
};

function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

function assetOptions(block: PresetBlock, assets: Asset[]) {
  const current = block.asset && !assets.some((asset) => asset.path === block.asset)
    ? [{ id: block.asset, filename: `${block.asset} (missing)`, path: block.asset }]
    : [];
  return [...current, ...assets];
}

export default function App() {
  const [theme, setTheme] = useState<"light" | "dark">("light");
  const [baseUrl, setBaseUrl] = useState(() => localStorage.getItem("ardor-manager.base-url") ?? "http://127.0.0.1:8080");
  const [token, setToken] = useState("");
  const [authEnabled, setAuthEnabled] = useState<boolean | null>(null);
  const [connected, setConnected] = useState(false);
  const [models, setModels] = useState<Asset[]>([]);
  const [irs, setIrs] = useState<Asset[]>([]);
  const [presets, setPresets] = useState<PresetSlotSummary[]>([]);
  const [selected, setSelected] = useState({ bank: 0, slot: 0 });
  const [savedPreset, setSavedPreset] = useState<Preset>(emptyPreset);
  const [draft, setDraft] = useState<Preset>(emptyPreset);
  const [status, setStatus] = useState("Not connected");
  const [busy, setBusy] = useState(false);

  const client = useMemo(() => new ArdorApiClient({ baseUrl, token: token || undefined }), [baseUrl, token]);
  const dirty = JSON.stringify(savedPreset) !== JSON.stringify(draft);

  async function run(action: () => Promise<void>) {
    setBusy(true);
    try {
      await action();
    } catch (error) {
      setStatus(error instanceof Error ? error.message : "Request failed");
    } finally {
      setBusy(false);
    }
  }

  function connect() {
    return run(async () => {
      const device = await client.getDevice();
      const [nextModels, nextIrs, nextPresets] = await Promise.all([
        client.listAssets("models"),
        client.listAssets("irs"),
        client.listPresets(),
      ]);
      setAuthEnabled(device.authEnabled);
      setConnected(true);
      localStorage.setItem("ardor-manager.base-url", baseUrl);
      setModels(nextModels);
      setIrs(nextIrs);
      setPresets(nextPresets);
      setStatus(`Connected to ${device.deviceName}`);
    });
  }

  function loadPreset(bank: number, slot: number) {
    return run(async () => {
      const response = await client.getPreset(bank, slot);
      setSelected({ bank, slot });
      setSavedPreset(response.preset);
      setDraft(response.preset);
      setStatus(`Loaded bank ${bank}, slot ${slot}`);
    });
  }

  function savePreset() {
    return run(async () => {
      const response = await client.savePreset(selected.bank, selected.slot, draft);
      setSavedPreset(response.preset);
      setDraft(response.preset);
      setPresets(await client.listPresets());
      setStatus("Preset saved");
    });
  }

  function applyPreset() {
    return run(async () => {
      await client.applyPreset(selected.bank, selected.slot);
      setStatus("Apply request sent");
    });
  }

  function upload(kind: AssetKind, file?: File) {
    if (!file) return;
    return run(async () => {
      await client.uploadAsset(kind, file, false);
      if (kind === "models") setModels(await client.listAssets(kind));
      if (kind === "irs") setIrs(await client.listAssets(kind));
      setStatus(`${file.name} uploaded`);
    });
  }

  return (
    <main data-theme={theme} className="min-h-screen bg-base-100 text-base-content">
      <header className="navbar border-b border-base-300 px-5">
        <div className="flex-1">
          <div className="text-lg font-semibold tracking-normal">Ardor Manager</div>
          <div className="ml-3 text-xs text-base-content/60">Preset and asset control</div>
        </div>
        <label className="flex items-center gap-2 text-sm">
          Dark mode
          <input
            type="checkbox"
            className="toggle toggle-sm"
            checked={theme === "dark"}
            onChange={(event) => setTheme(event.target.checked ? "dark" : "light")}
          />
        </label>
      </header>

      <div className="grid min-h-[calc(100vh-65px)] grid-cols-1 gap-4 p-4 lg:grid-cols-[260px_minmax(0,1fr)_300px]">
        <aside className="space-y-4 border-b border-base-300 pb-4 lg:border-b-0 lg:border-r lg:pr-4">
          <section className="space-y-2">
            <div className="flex items-center justify-between">
              <h2 className="text-xs font-semibold uppercase tracking-normal text-base-content/60">Connection</h2>
              <span className={`badge badge-sm ${connected ? "badge-success" : "badge-ghost"}`}>
                {connected ? "Connected" : "Offline"}
              </span>
            </div>
            <input className="input input-bordered input-sm w-full" aria-label="Device URL" value={baseUrl} onChange={(event) => setBaseUrl(event.target.value)} />
            <input className="input input-bordered input-sm w-full" aria-label="Bearer token" type="password" placeholder="Bearer token" value={token} onChange={(event) => setToken(event.target.value)} />
            <button className="btn btn-primary btn-sm w-full" onClick={connect} disabled={busy}>Connect</button>
            {authEnabled !== null && <div className="text-xs text-base-content/60">Device auth: {authEnabled ? "enabled" : "disabled"}</div>}
          </section>

          <section className="space-y-2">
            <h2 className="text-xs font-semibold uppercase tracking-normal text-base-content/60">Preset slots</h2>
            <div className="grid grid-cols-4 gap-1">
              {[0, 1, 2, 3].map((slot) => (
                <button key={slot} className="btn btn-outline btn-xs" onClick={() => loadPreset(0, slot)} disabled={!connected || busy}>0-{slot}</button>
              ))}
            </div>
            <div className="max-h-64 overflow-auto rounded border border-base-300">
              {presets.filter((preset) => preset.exists).map((preset) => (
                <button key={`${preset.bank}-${preset.slot}`} className="btn btn-ghost btn-sm h-auto min-h-9 w-full justify-start rounded-none px-2 py-1 text-left" onClick={() => loadPreset(preset.bank, preset.slot)} disabled={busy}>
                  <span className="w-10 shrink-0 font-mono text-xs">{preset.bank}-{preset.slot}</span>
                  <span className="truncate">{preset.name ?? "Unnamed"}</span>
                </button>
              ))}
              {presets.every((preset) => !preset.exists) && <div className="p-3 text-xs text-base-content/60">No saved presets found.</div>}
            </div>
          </section>
        </aside>

        <section className="min-w-0 space-y-4">
          <div className="flex flex-wrap items-center justify-between gap-3 border-b border-base-300 pb-3">
            <div>
              <div className="text-xs text-base-content/60">Bank {selected.bank}, slot {selected.slot}</div>
              <h1 className="text-xl font-semibold">{draft.name}</h1>
            </div>
            <div className="flex flex-wrap items-center gap-2">
              {dirty && <span className="badge badge-warning">Unsaved changes</span>}
              <button className="btn btn-ghost btn-sm" onClick={() => setDraft(savedPreset)} disabled={!dirty || busy}>Discard</button>
              <button className="btn btn-primary btn-sm" onClick={savePreset} disabled={!connected || !dirty || busy}>Save</button>
              <button className="btn btn-outline btn-sm" onClick={applyPreset} disabled={!connected || dirty || busy}>Apply</button>
            </div>
          </div>

          <div className="grid gap-3 sm:grid-cols-3">
            <label className="form-control">
              <span className="label-text text-xs">Preset name</span>
              <input className="input input-bordered input-sm" value={draft.name} onChange={(event) => setDraft(setPresetName(draft, event.target.value))} />
            </label>
            <label className="form-control">
              <span className="label-text text-xs">Input gain (dB)</span>
              <input type="number" className="input input-bordered input-sm" value={draft.global.inputGainDb} onChange={(event) => setDraft(setGlobalParam(draft, "inputGainDb", Number(event.target.value)))} />
            </label>
            <label className="form-control">
              <span className="label-text text-xs">Output gain (dB)</span>
              <input type="number" className="input input-bordered input-sm" value={draft.global.outputGainDb} onChange={(event) => setDraft(setGlobalParam(draft, "outputGainDb", Number(event.target.value)))} />
            </label>
          </div>

          <div className="space-y-3">
            {draft.blocks.map((block, index) => {
              const editable = isKnownEditableBlock(block);
              const blockAssets = block.type === "nam" ? models : irs;
              const params = effectParams[block.type] ?? (block.type === "cab" ? ["levelDb", "mix"] : []);
              return (
                <article key={block.id} className="border border-base-300 p-3">
                  <div className="mb-3 flex items-center justify-between gap-2">
                    <div className="flex items-center gap-2">
                      <span className="badge badge-neutral">{index + 1}</span>
                      <h2 className="font-medium">{block.type}</h2>
                      <span className="text-xs text-base-content/50">{block.id}</span>
                    </div>
                    {!editable && <span className="badge badge-outline">Read-only</span>}
                  </div>
                  {editable ? (
                    <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-3">
                      {(block.type === "nam" || block.type === "cab") && (
                        <label className="form-control sm:col-span-2 lg:col-span-1">
                          <span className="label-text text-xs">{block.type === "nam" ? "NAM model" : "Cabinet IR"}</span>
                          <select className="select select-bordered select-sm" value={block.asset} onChange={(event) => setDraft(setBlockAsset(draft, block.id, event.target.value))}>
                            <option value="">No asset</option>
                            {assetOptions(block, blockAssets).map((asset) => <option key={asset.id} value={asset.path}>{asset.filename}</option>)}
                          </select>
                        </label>
                      )}
                      {params.map((key) => (
                        <label key={key} className="form-control">
                          <span className="label-text text-xs">{key}</span>
                          <input type="number" step="0.01" min={key === "mix" ? 0 : undefined} max={key === "mix" ? 1 : undefined} className="input input-bordered input-sm" value={Number(block.params[key] ?? (key === "mix" ? 1 : 0))} onChange={(event) => setDraft(setBlockParam(draft, block.id, key, Number(event.target.value)))} />
                        </label>
                      ))}
                    </div>
                  ) : (
                    <pre className="overflow-auto bg-base-200 p-2 text-xs">{JSON.stringify(block, null, 2)}</pre>
                  )}
                </article>
              );
            })}
            {draft.blocks.length === 0 && <div className="border border-dashed border-base-300 p-8 text-center text-sm text-base-content/60">This preset has no blocks.</div>}
          </div>
        </section>

        <aside className="space-y-4 border-t border-base-300 pt-4 lg:border-l lg:border-t-0 lg:pl-4 lg:pt-0">
          <section className="space-y-2">
            <h2 className="text-xs font-semibold uppercase tracking-normal text-base-content/60">Upload assets</h2>
            <label className="form-control">
              <span className="label-text text-xs">NAM model</span>
              <input type="file" accept=".nam" className="file-input file-input-bordered file-input-sm w-full" onChange={(event) => upload("models", event.target.files?.[0])} />
            </label>
            <label className="form-control">
              <span className="label-text text-xs">Cabinet IR</span>
              <input type="file" accept=".wav" className="file-input file-input-bordered file-input-sm w-full" onChange={(event) => upload("irs", event.target.files?.[0])} />
            </label>
          </section>
          <section className="space-y-2">
            <h2 className="text-xs font-semibold uppercase tracking-normal text-base-content/60">Device assets</h2>
            <AssetList title="Models" assets={models} />
            <AssetList title="IRs" assets={irs} />
          </section>
          <div className="alert alert-info py-2 text-xs">{status}</div>
        </aside>
      </div>
    </main>
  );
}

function AssetList({ title, assets }: { title: string; assets: Asset[] }) {
  return (
    <div>
      <div className="mb-1 text-sm font-medium">{title}</div>
      {assets.length === 0 ? <div className="text-xs text-base-content/50">None</div> : assets.map((asset) => (
        <div key={asset.id} className="flex items-center justify-between gap-2 border-b border-base-300 py-1 text-xs">
          <span className="truncate">{asset.filename}</span>
          <span className="shrink-0 text-base-content/50">{formatSize(asset.sizeBytes)}</span>
        </div>
      ))}
    </div>
  );
}
