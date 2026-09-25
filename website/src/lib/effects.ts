import data from '../data/effects.generated.json';
import { effectCopy } from '../data/effect-copy';

export type Control = {
  key: string;
  label: string;
  kind: string;
  unit: string;
  min?: number;
  max?: number;
  step?: number;
  default?: number | string | boolean;
  choices?: string[];
  assetKind?: string;
  assetLabel?: string;
};

export type Definition = {
  id: string;
  name: string;
  description: string;
  category: string;
  blockType: string;
  mode: string | null;
  maxEnabledInGroup: number | null;
  controls: Control[];
};

export type CategoryMeta = { label: string; order: number; effect: boolean };

export type Counts = {
  total: number;
  effects: number;
  amp: number;
  cabinet: number;
  drive: number;
  utility: number;
  modulation: number;
  delay: number;
  reverb: number;
};

const defs = data.definitions as Definition[];

/** Description with the editorial override applied when present. */
export function describe(def: Definition): string {
  return effectCopy[def.id] ?? def.description;
}

export function byCategory(category: string): Definition[] {
  return defs.filter((d) => d.category === category);
}

export const counts = data.counts as Counts;
export const categoryMeta = data.categoryMeta as Record<string, CategoryMeta>;
export const allDefinitions = defs;

/**
 * The effect families in display order, with the colour token the pedal uses
 * for each. Drive shares the amp colour on the device (see LvglUiStyle.cpp).
 */
export type EffectFamily = {
  key: string;
  label: string;
  color: string;
  definitions: Definition[];
};

const FAMILY_COLOR: Record<string, string> = {
  drive: 'var(--family-amp)',
  utility: 'var(--family-util)',
  modulation: 'var(--family-mod)',
  delay: 'var(--family-delay)',
  reverb: 'var(--family-reverb)',
};

export const effectFamilies: EffectFamily[] = Object.entries(categoryMeta)
  .filter(([, meta]) => meta.effect)
  .sort((a, b) => a[1].order - b[1].order)
  .map(([key, meta]) => ({
    key,
    label: meta.label,
    color: FAMILY_COLOR[key] ?? 'var(--family-util)',
    definitions: byCategory(key),
  }));
