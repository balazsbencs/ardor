import { useEffect, useMemo, useState } from "react";

import type { Preset } from "../api/types";
import { useDeviceSession } from "../connection/deviceSession";
import type { PresetLocation } from "../presets/editor/editorTypes";

/** The presets of one bank for the tiles. The draft replaces its own saved slot. */
export function useBankPresets(bank: number, draft?: { location: PresetLocation; preset: Preset }): Map<number, Preset> {
  const session = useDeviceSession();
  const [loaded, setLoaded] = useState<Map<number, Preset>>(new Map());
  const existing = useMemo(() => session.presets.filter((summary) => summary.bank === bank && summary.exists).map(({ slot }) => slot).join(","), [session.presets, bank]);

  useEffect(() => {
    let cancelled = false;
    const client = session.client;
    const slots = existing ? existing.split(",").map(Number) : [];
    const skip = draft && draft.location.bank === bank ? draft.location.slot : -1;
    if (!client) return undefined;
    void Promise.all(slots.filter((slot) => slot !== skip).map(async (slot) => [slot, (await client.getPreset(bank, slot)).preset] as const))
      .then((entries) => { if (!cancelled) setLoaded(new Map(entries)); })
      .catch(() => { if (!cancelled) setLoaded(new Map()); });
    return () => { cancelled = true; };
    // eslint-disable-next-line react-hooks/exhaustive-deps -- the draft is keyed by location so edits do not refetch
  }, [session.client, bank, existing, draft?.location.bank, draft?.location.slot]);

  return useMemo(() => {
    const merged = new Map(loaded);
    if (draft && draft.location.bank === bank) merged.set(draft.location.slot, draft.preset);
    return merged;
  }, [loaded, draft, bank]);
}
