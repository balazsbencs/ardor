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

const defs = data.definitions as Definition[];

/** Description with the editorial override applied when present. */
export function describe(def: Definition): string {
  return effectCopy[def.id] ?? def.description;
}

export function byCategory(category: string): Definition[] {
  return defs.filter((d) => d.category === category);
}

export const counts = data.counts;
export const categoryMeta = data.categoryMeta as Record<string, { label: string; order: number }>;
export const allDefinitions = defs;

/** The 35 hosted effect algorithms (modulation + delay + reverb). */
export const hostedEffects = defs.filter((d) =>
  ['modulation', 'delay', 'reverb'].includes(d.category),
);

/** Ordered category keys for display. */
export const orderedCategories = Object.entries(categoryMeta)
  .sort((a, b) => a[1].order - b[1].order)
  .map(([key]) => key);
