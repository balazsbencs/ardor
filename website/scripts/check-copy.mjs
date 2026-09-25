// Copy checker for the built site. Crawls dist/ HTML and fails when visible
// text breaks a rule the copy review set:
//   - no em dash or en dash in visible text (the site copy follows STE),
//   - every "<n> effects" total matches the effect catalog,
//   - every catalog block appears on the effects reference page,
//   - no stale or wrong claims (see BANNED below).
import { readFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { join, relative, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const websiteRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const dist = join(websiteRoot, 'dist');
const catalogPath = join(websiteRoot, 'src/data/effects.generated.json');

// Phrases that were wrong or stale on the old site.
const BANNED = [/Neural DSP/i, /Open source power/i];

// Families that are not effects: the amp and the cabinet are the rig itself.
const NON_EFFECT_CATEGORIES = new Set(['amp', 'cabinet']);

if (!existsSync(dist)) {
  console.error('[check-copy] dist/ not found. Run `npm run build` first.');
  process.exit(1);
}

function walk(dir) {
  const out = [];
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) out.push(...walk(p));
    else if (name.endsWith('.html')) out.push(p);
  }
  return out;
}

/** Visible text only: drop scripts, styles, comments, code, and tags. */
function visibleText(html) {
  return html
    .replace(/<!--[\s\S]*?-->/g, ' ')
    .replace(/<(script|style|pre|code|svg)\b[\s\S]*?<\/\1>/gi, ' ')
    .replace(/<[^>]+>/g, ' ')
    .replace(/&nbsp;/g, ' ')
    .replace(/&amp;/g, '&')
    .replace(/\s+/g, ' ');
}

function context(text, index) {
  return text.slice(Math.max(0, index - 40), index + 40).trim();
}

const catalog = JSON.parse(readFileSync(catalogPath, 'utf8'));
const effectDefs = catalog.definitions.filter((d) => !NON_EFFECT_CATEGORIES.has(d.category));
const expectedTotal = effectDefs.length;

const failures = [];
const files = walk(dist);

for (const file of files) {
  const rel = relative(dist, file);
  const text = visibleText(readFileSync(file, 'utf8'));

  for (const match of text.matchAll(/[–—]/g)) {
    failures.push(`${rel}: dash "${match[0]}" in "${context(text, match.index)}"`);
  }

  for (const match of text.matchAll(/\b(\d+)\s+(?:built-in\s+)?effects\b/gi)) {
    if (Number(match[1]) !== expectedTotal) {
      failures.push(`${rel}: says ${match[1]} effects, catalog has ${expectedTotal}`);
    }
  }

  for (const pattern of BANNED) {
    const match = text.match(pattern);
    if (match) failures.push(`${rel}: banned phrase "${match[0]}"`);
  }
}

const effectsPage = join(dist, 'docs/effects/index.html');
if (!existsSync(effectsPage)) {
  failures.push('docs/effects/index.html is missing');
} else {
  const text = visibleText(readFileSync(effectsPage, 'utf8'));
  for (const def of catalog.definitions) {
    if (!text.includes(def.name)) failures.push(`docs/effects: block "${def.name}" is not listed`);
  }
}

if (failures.length > 0) {
  console.error(`[check-copy] ${failures.length} problem(s):`);
  for (const f of failures) console.error(`  - ${f}`);
  process.exit(1);
}
console.log(`[check-copy] ${files.length} pages OK (${expectedTotal} effects in the catalog)`);
