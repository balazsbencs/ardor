// Join a site-relative path onto Astro's configured base so links stay correct
// under the GitHub Pages sub-path (/ardor) and at the root in dev alike.
const BASE = import.meta.env.BASE_URL; // e.g. "/ardor/" or "/"

export function url(path = ''): string {
  const base = BASE.endsWith('/') ? BASE.slice(0, -1) : BASE;
  if (!path || path === '/') return base || '/';
  const clean = path.startsWith('/') ? path : `/${path}`;
  return `${base}${clean}`;
}

// Absolute path comparison helper for nav active-state.
export function isActive(current: string, path: string): boolean {
  const target = url(path);
  const norm = (s: string) => (s.endsWith('/') && s.length > 1 ? s.slice(0, -1) : s);
  return norm(current) === norm(target);
}
