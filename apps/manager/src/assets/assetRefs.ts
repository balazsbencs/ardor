import type { Asset, AssetKind, Preset, PresetBlock } from "../api/types";

export type AssetRef = { blockId: string; path: string; kind: AssetKind };

const KIND_BY_TYPE: Record<string, AssetKind> = { nam: "models", cab: "irs", irreverb: "reverb-irs" };
const DUAL_AMP_KEYS: Array<[string, AssetKind]> = [["leftNamAsset", "models"], ["leftIrAsset", "irs"], ["rightNamAsset", "models"], ["rightIrAsset", "irs"]];

function walk(blocks: PresetBlock[], out: AssetRef[]): AssetRef[] {
  for (const block of blocks) {
    const kind = KIND_BY_TYPE[block.type];
    if (kind && block.asset) out.push({ blockId: block.id, path: block.asset, kind });
    if (block.type === "dualAmp") {
      for (const [key, dualKind] of DUAL_AMP_KEYS) {
        const value = block.params[key];
        if (typeof value === "string" && value) out.push({ blockId: block.id, path: value, kind: dualKind });
      }
    }
    if (block.lanes) { walk(block.lanes.left.blocks, out); walk(block.lanes.right.blocks, out); }
  }
  return out;
}

/** Every file a preset references, in chain order. Mirrors collectAssetPaths in managerd. */
export function assetRefs(preset: Preset): AssetRef[] {
  const refs = walk(preset.blocks, []);
  if (preset.wdw) { walk(preset.wdw.dry.blocks, refs); walk(preset.wdw.wet.blocks, refs); }
  return refs;
}

export function missingPaths(preset: Preset, inventory: { models: Asset[]; irs: Asset[]; reverbIrs: Asset[] }): Set<string> {
  const known = new Set([...inventory.models, ...inventory.irs, ...inventory.reverbIrs].map(({ path }) => path));
  return new Set(assetRefs(preset).map(({ path }) => path).filter((path) => !known.has(path)));
}
