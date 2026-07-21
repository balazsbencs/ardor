/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly TONE3000_CLIENT_ID?: string;
  readonly TONE3000_BASE_URL?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
