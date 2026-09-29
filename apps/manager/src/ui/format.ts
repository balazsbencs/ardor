export const bankLabel = (bank: number): string => `BANK ${String(bank).padStart(2, "0")}`;
export const slotLabel = (slot: number): string => `FS ${slot + 1}`;

const DISPLAY_UNITS = "%|x|×|dBFS|dB|BPM|°|st|s|oct|ms|kHz|Hz|bit";
const DISPLAY_PATTERN = new RegExp(`^([+-]?[\\d.,]+)\\s*(${DISPLAY_UNITS})$`);

/** Splits "412 ms" so the number and the unit can use different type. Only known units split. */
export function splitDisplay(text: string): { value: string; unit: string } {
  const match = DISPLAY_PATTERN.exec(text.trim());
  return match ? { value: match[1], unit: match[2] } : { value: text, unit: "" };
}

export function fileSize(bytes: number): string {
  if (bytes < 1024 * 1024) return `${Math.max(1, Math.round(bytes / 1024))} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

export const fileStem = (filename: string): string => filename.replace(/\.(nam|wav)$/i, "");
