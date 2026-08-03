import { isTauri } from "@tauri-apps/api/core";

const desktopDefaultBaseUrl = "http://127.0.0.1:8080";

export function isHostedCloudRuntime(): boolean {
  return import.meta.env.VITE_ARDOR_HOSTED_MODE === "true";
}

export function isDeviceHostedRuntime(): boolean {
  return !isHostedCloudRuntime() && !isTauri() && (import.meta.env.PROD || import.meta.env.VITE_DEVICE_HOSTED === "true");
}

export function defaultDeviceBaseUrl(): string {
  return isDeviceHostedRuntime() ? window.location.origin : desktopDefaultBaseUrl;
}
