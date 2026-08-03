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
    base.choices = (control.choices ?? []).map((c) => c.label ?? c.value);
    base.default = control.defaultValue;
  }
  if (control.kind === 'toggle') {
    base.default = control.defaultValue;
  }
  if (control.kind === 'asset') {
    base.assetKind = control.assetKind;
  }
  return base;
}

const CATEGORY_META = {
  amp: { label: 'Amp', order: 0 },
  cabinet: { label: 'Cabinet', order: 1 },
  utility: { label: 'Utility', order: 2 },
  modulation: { label: 'Modulation', order: 3 },
  delay: { label: 'Delay', order: 4 },
  reverb: { label: 'Reverb', order: 5 },
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

const out = {
  version: catalog.version,
  generatedFrom: 'apps/manager/src/effects/catalog.v1.json',
  categoryMeta: CATEGORY_META,
  categories,
  definitions,
  counts: {
    total: definitions.length,
    modulation: (categories.modulation ?? []).length,
    delay: (categories.delay ?? []).length,
    reverb: (categories.reverb ?? []).length,
  },
};

mkdirSync(dirname(outPath), { recursive: true });
writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
console.log(
  `[gen-effects] wrote ${definitions.length} definitions ` +
    `(${out.counts.modulation} mod / ${out.counts.delay} delay / ${out.counts.reverb} reverb)`,
);
