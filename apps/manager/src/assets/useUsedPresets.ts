import { useEffect, useMemo, useState } from "react";

import type { AssetUse, Preset } from "../api/types";
import { useDeviceSession } from "../connection/deviceSession";
import { createEmptyPreset } from "../presets/editor/presetFactory";
import { presetName } from "./kindInfo";

const keyOf = ({ bank, slot }: { bank: number; slot: number }) => `${bank}:${slot}`;

/** The presets that use a file, for their tiles. A tile shows the name until the preset itself loads. */
export function useUsedPresets(uses: AssetUse[]): (use: AssetUse) => Preset {
  const { client } = useDeviceSession();
  const [loaded, setLoaded] = useState<Map<string, Preset>>(new Map());
  const wanted = uses.map(keyOf).join(",");

  useEffect(() => {
    let cancelled = false;
    void Promise.allSettled(uses.map(async (use) => [keyOf(use), (await client!.getPreset(use.bank, use.slot)).preset] as const))
      .then((results) => {
        if (cancelled) return;
        setLoaded(new Map(results.flatMap((result) => (result.status === "fulfilled" ? [result.value] : []))));
      });
    return () => { cancelled = true; };
    // eslint-disable-next-line react-hooks/exhaustive-deps -- keyed by the used locations
  }, [client, wanted]);

  return useMemo(() => (use) => loaded.get(keyOf(use)) ?? createEmptyPreset(presetName(use)), [loaded]);
}
