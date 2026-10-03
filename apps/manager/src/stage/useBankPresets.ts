import { useEffect, useMemo, useState } from "react";

import type { Preset } from "../api/types";
import { useDeviceSession } from "../connection/deviceSession";
import type { PresetLocation } from "../presets/editor/editorTypes";

/** The presets of one bank for the tiles. The draft replaces its own saved slot. */
export function useBankPresets(bank: number, draft?: { location: PresetLocation; preset: Preset }): Map<number, Preset> {
  const session = useDeviceSession();
  const [loaded, setLoaded] = useState<{ bank: number; presets: Map<number, Preset> }>({ bank, presets: new Map() });
  const existing = useMemo(() => session.presets.filter((summary) => summary.bank === bank && summary.exists).map(({ slot }) => slot).join(","), [session.presets, bank]);

  useEffect(() => {
    let cancelled = false;
    const client = session.client;
    const slots = existing ? existing.split(",").map(Number) : [];
    const skip = draft && draft.location.bank === bank ? draft.location.slot : -1;
    if (!client) return undefined;
    void Promise.allSettled(slots.filter((slot) => slot !== skip).map(async (slot) => [slot, (await client.getPreset(bank, slot)).preset] as const))
      .then((results) => {
        if (cancelled) return;
        const entries = results.flatMap((result) => (result.status === "fulfilled" ? [result.value] : []));
        setLoaded({ bank, presets: new Map(entries) });
      });
    return () => { cancelled = true; };
    // eslint-disable-next-line react-hooks/exhaustive-deps -- the draft is keyed by location so edits do not refetch
  }, [session.client, bank, existing, draft?.location.bank, draft?.location.slot]);

  return useMemo(() => {
    const merged = new Map(loaded.bank === bank ? loaded.presets : []);
    if (draft && draft.location.bank === bank) merged.set(draft.location.slot, draft.preset);
    return merged;
  }, [loaded, draft, bank]);
}
