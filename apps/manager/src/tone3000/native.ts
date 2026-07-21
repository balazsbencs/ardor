import { invoke, isTauri } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

export function tone3000NativeAvailable(): boolean {
  return isTauri();
}

export async function openTone3000(url: string): Promise<void> {
  if (!isTauri()) throw new Error("Tone3000 browsing is available in the Ardor desktop app.");
  await invoke("open_tone3000", { url });
}

export async function cancelTone3000(): Promise<void> {
  if (isTauri()) await invoke("cancel_tone3000");
}

export async function onTone3000Callback(handler: (url: string) => void): Promise<() => void> {
  if (!isTauri()) return () => undefined;
  return listen<string>("tone3000-oauth-callback", ({ payload }) => handler(payload));
}
