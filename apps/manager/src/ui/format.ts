export const bankLabel = (bank: number): string => `BANK ${String(bank).padStart(2, "0")}`;
export const slotLabel = (slot: number): string => `FS ${slot + 1}`;

/** Splits "412 ms" so the number and the unit can use different type. */
export function splitDisplay(text: string): { value: string; unit: string } {
  const match = /^([+-]?[\d.,:]+)\s*([^\d\s].*)?$/.exec(text.trim());
  return match ? { value: match[1], unit: match[2] ?? "" } : { value: text, unit: "" };
}

export function fileSize(bytes: number): string {
  if (bytes < 1024 * 1024) return `${Math.max(1, Math.round(bytes / 1024))} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

export const fileStem = (filename: string): string => filename.replace(/\.(nam|wav)$/i, "");
