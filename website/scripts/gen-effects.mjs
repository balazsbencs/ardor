// Vendors the manager's authoritative effect catalog into the website build.
//
// The site is scoped to website/, but the block catalog lives at
// apps/manager/src/effects/catalog.v1.json and is the single source of truth
// for block names, categories, and parameters. This script reads it and emits
// website/src/data/effects.generated.json so Astro pages import a stable,
// in-tree artifact. It runs automatically before dev/build/check.
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const websiteRoot = resolve(here, '..');
const repoRoot = resolve(websiteRoot, '..');
const catalogPath = resolve(repoRoot, 'apps/manager/src/effects/catalog.v1.json');
const outPath = resolve(websiteRoot, 'src/data/effects.generated.json');

if (!existsSync(catalogPath)) {
  console.error(`[gen-effects] catalog not found at ${catalogPath}`);
  process.exit(1);
}

const catalog = JSON.parse(readFileSync(catalogPath, 'utf8'));

const UNIT_LABEL = { db: 'dB', percent: '%', ratio: ':1', ms: 'ms', hz: 'Hz' };

// Player-facing names for asset kinds; the catalog keys are storage folder names.
const ASSET_LABEL = {
  models: 'NAM capture (.nam)',
  irs: 'Cabinet impulse response',
  'reverb-irs': 'Reverb impulse response',
};

/** Turn one catalog control into a compact, display-ready parameter record. */
function normalizeControl(control) {
  const unit = control.unit ? UNIT_LABEL[control.unit] ?? control.unit : '';
  const base = {
    key: control.key ?? control.kind,
    label: control.label ?? control.key ?? control.kind,
    kind: control.kind,
    unit,
  };
  if (control.kind === 'number') {
    base.min = control.minimum;
    base.max = control.maximum;
    base.step = control.step;
    base.default = control.defaultValue;
  }
  if (control.kind === 'choice') {
    const choices = control.choices ?? [];
    base.choices = choices.map((c) => c.label ?? c.value);
    // Show the default by its label ("L+R Average"), not its stored value ("sum").
    const match = choices.find((c) => c.value === control.defaultValue);
    base.default = match ? (match.label ?? match.value) : control.defaultValue;
  }
  if (control.kind === 'toggle') {
    base.default = control.defaultValue;
  }
  if (control.kind === 'asset') {
    base.assetKind = control.assetKind;
    base.assetLabel = ASSET_LABEL[control.assetKind] ?? control.assetKind;
  }
  return base;
}

// Every catalog category must be listed here, or its blocks disappear from the
// site. The script fails on an unknown category for that reason.
const CATEGORY_META = {
  amp: { label: 'Amp', order: 0, effect: false },
  cabinet: { label: 'Cabinet', order: 1, effect: false },
  drive: { label: 'Drive', order: 2, effect: true },
  utility: { label: 'Dynamics and tone', order: 3, effect: true },
  modulation: { label: 'Modulation', order: 4, effect: true },
  delay: { label: 'Delay', order: 5, effect: true },
  reverb: { label: 'Reverb', order: 6, effect: true },
};

const definitions = catalog.definitions.map((def) => ({
  id: def.id,
  name: def.name,
  description: def.description,
  category: def.category,
  blockType: def.blockType,
  mode: def.mode ?? null,
  maxEnabledInGroup: def.maxEnabledInGroup ?? null,
  controls: (def.controls ?? []).map(normalizeControl),
}));

const categories = {};
for (const def of definitions) {
  (categories[def.category] ??= []).push(def.id);
}

const unknown = Object.keys(categories).filter((key) => !(key in CATEGORY_META));
if (unknown.length > 0) {
  console.error(`[gen-effects] add these categories to CATEGORY_META: ${unknown.join(', ')}`);
  process.exit(1);
}

const countOf = (key) => (categories[key] ?? []).length;
const effectTotal = Object.entries(CATEGORY_META)
  .filter(([, meta]) => meta.effect)
  .reduce((sum, [key]) => sum + countOf(key), 0);

const out = {
  version: catalog.version,
  generatedFrom: 'apps/manager/src/effects/catalog.v1.json',
  categoryMeta: CATEGORY_META,
  categories,
  definitions,
  counts: {
    total: definitions.length,
    effects: effectTotal,
    ...Object.fromEntries(Object.keys(CATEGORY_META).map((key) => [key, countOf(key)])),
  },
};

mkdirSync(dirname(outPath), { recursive: true });
writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
console.log(
  `[gen-effects] wrote ${definitions.length} definitions ` +
    `(${out.counts.effects} effects: ${out.counts.drive} drive / ${out.counts.utility} dynamics / ` +
    `${out.counts.modulation} mod / ${out.counts.delay} delay / ${out.counts.reverb} reverb)`,
);
