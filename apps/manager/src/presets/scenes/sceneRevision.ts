import type { DeviceStatus } from "../../api/types";
import type { PresetLocation } from "../editor/editorTypes";

export function activeRevisionMatchesDraft(
  device: DeviceStatus | undefined,
  location: PresetLocation,
  dirty: boolean,
  revision?: string,
): boolean {
  const active = device?.active;
  return !dirty
    && active?.bank === location.bank
    && active.slot === location.slot
    && active.generation !== undefined
    && active.storedRevisionMatches === true
    && active.revision === revision;
}
