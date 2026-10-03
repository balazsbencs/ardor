import type { Asset, AssetKind, AssetUse, AssetUsageEntry } from "../api/types";

const kindOfPath = (path: string): AssetKind | undefined =>
  path.startsWith("models/") ? "models" : path.startsWith("irs/") ? "irs" : path.startsWith("reverb-irs/") ? "reverb-irs" : undefined;

export function usedBy(usage: AssetUsageEntry[] | undefined, path: string): AssetUse[] | undefined {
  if (!usage) return undefined;
  return usage.find((entry) => entry.path === path)?.presets ?? [];
}

export function missingFiles(usage: AssetUsageEntry[] | undefined, inventory: { models: Asset[]; irs: Asset[]; reverbIrs: Asset[] }) {
  if (!usage) return [];
  const known = new Set([...inventory.models, ...inventory.irs, ...inventory.reverbIrs].map(({ path }) => path));
  return usage.flatMap((entry) => {
    const kind = kindOfPath(entry.path);
    return kind && !known.has(entry.path) ? [{ path: entry.path, kind, presets: entry.presets }] : [];
  });
}
