/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly VITE_DEVICE_HOSTED?: string;
  readonly VITE_ARDOR_HOSTED_MODE?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
