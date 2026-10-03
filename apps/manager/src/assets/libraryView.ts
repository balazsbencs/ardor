import type { Asset, AssetKind, AssetUsageEntry } from "../api/types";
import { usedBy } from "./assetUsage";

export type AssetSort = "name" | "size" | "used";

export const KIND_LABELS: Record<AssetKind, string> = { models: "NAM models", irs: "Cabinet IRs", "reverb-irs": "Reverb IRs" };
export const KIND_EXTENSIONS: Record<AssetKind, ".nam" | ".wav"> = { models: ".nam", irs: ".wav", "reverb-irs": ".wav" };

/** Filters by file name and orders: name A-Z, size largest first, or most used first (then name). */
export function visibleAssets(files: Asset[], query: string, sort: AssetSort, usage: AssetUsageEntry[] | undefined): Asset[] {
  const needle = query.trim().toLowerCase();
  const useCount = (asset: Asset) => usedBy(usage, asset.path)?.length ?? 0;
  const byName = (a: Asset, b: Asset) => a.filename.localeCompare(b.filename);
  const order = sort === "size" ? (a: Asset, b: Asset) => b.sizeBytes - a.sizeBytes || byName(a, b)
    : sort === "used" ? (a: Asset, b: Asset) => useCount(b) - useCount(a) || byName(a, b)
    : byName;
  return files.filter((asset) => asset.filename.toLowerCase().includes(needle)).sort(order);
}
