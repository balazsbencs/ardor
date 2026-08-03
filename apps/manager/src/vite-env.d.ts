/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly TONE3000_CLIENT_ID?: string;
  readonly TONE3000_BASE_URL?: string;
  readonly VITE_DEVICE_HOSTED?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
