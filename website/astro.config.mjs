// @ts-check
import { defineConfig } from 'astro/config';

// Ardor ships as a GitHub Pages project site at balazsbencs.github.io/ardor.
// `site` + `base` keep generated links correct in production while `astro dev`
// serves everything under the same base path for parity.
export default defineConfig({
  site: 'https://balazsbencs.github.io',
  base: '/ardor',
  trailingSlash: 'ignore',
  build: {
    format: 'directory',
  },
  vite: {
    build: {
      // three.js is large; a dedicated chunk keeps the rest of the site lean.
      chunkSizeWarningLimit: 900,
    },
  },
});
