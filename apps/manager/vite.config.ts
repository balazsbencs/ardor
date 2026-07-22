import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

export default defineConfig({
  plugins: [react()],
  envPrefix: ["VITE_", "TONE3000_"],
  clearScreen: false,
  server: { port: 1420, strictPort: true },
  test: {
    environment: "jsdom",
    setupFiles: ["./src/test/setup.ts"],
    css: true,
    // OAuth uses a publishable application identifier, but unit tests must not
    // depend on a developer machine or CI environment having one configured.
    env: { TONE3000_CLIENT_ID: "t3k_test_client" },
  },
});
