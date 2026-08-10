// Internal link checker for the built site. Crawls dist/ HTML, resolves every
// same-site href/src to a file on disk, and fails if any target is missing.
// External links (http/https/mailto) and pure #fragments are skipped.
import { readFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { join, relative, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const websiteRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const dist = join(websiteRoot, 'dist');

// Base path must match astro.config.mjs.
const BASE = '/ardor';

if (!existsSync(dist)) {
  console.error('[check-links] dist/ not found — run `npm run build` first.');
  process.exit(1);
}

function walk(dir) {
  const out = [];
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    const s = statSync(p);
    if (s.isDirectory()) out.push(...walk(p));
    else if (name.endsWith('.html')) out.push(p);
  }
  return out;
}

/** Does a same-site path resolve to a real file in dist? */
function resolves(pathname) {
  let rel = pathname;
  if (rel.startsWith(BASE)) rel = rel.slice(BASE.length);
  rel = rel.replace(/^\/+/, '').split('#')[0].split('?')[0];
  if (rel === '' || rel.endsWith('/')) rel += 'index.html';
  const candidates = [
    join(dist, rel),
    join(dist, rel, 'index.html'),
    join(dist, `${rel}.html`),
  ];
  return candidates.some((c) => existsSync(c) && statSync(c).isFile());
}

const files = walk(dist);
const attrRe = /(?:href|src)\s*=\s*"([^"]+)"/gi;
let checked = 0;
const broken = [];

for (const file of files) {
  const html = readFileSync(file, 'utf8');
  let m;
  while ((m = attrRe.exec(html))) {
    const link = m[1];
    if (
      link.startsWith('http://') ||
      link.startsWith('https://') ||
      link.startsWith('mailto:') ||
      link.startsWith('data:') ||
      link.startsWith('#') ||
      link.startsWith('//')
    ) {
      continue;
    }
    // Only check same-site absolute paths and relative page/asset links.
    const pathname = link.startsWith('/')
      ? link
      : `${BASE}/${relative(dist, resolve(dirname(file), link)).split('#')[0]}`;
    checked++;
    if (!resolves(pathname)) {
      broken.push({ file: relative(dist, file), link });
    }
  }
}

if (broken.length) {
  console.error(`[check-links] ${broken.length} broken internal link(s):`);
  for (const b of broken) console.error(`  ${b.file} → ${b.link}`);
  process.exit(1);
}
console.log(`[check-links] OK — ${checked} internal links across ${files.length} pages.`);
