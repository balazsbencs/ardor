import { useState } from "react";

import type { PresetBlock } from "../api/types";
import { Toggle } from "../components/ui";
import type { NumberControl } from "../effects/types";
import type { EqBand, EqPassFilter } from "../presets/editor/editorTypes";
import { EqResponseGraph, eqStateFor } from "../presets/inspector/EqResponseGraph";
import { TravelScale } from "../ui/TravelScale";

const FREQUENCY: NumberControl = { kind: "number", key: "frequency_hz", label: "Frequency", minimum: 20, maximum: 20000, step: 1, unit: "hz", defaultValue: 1000 };
const GAIN: NumberControl = { kind: "number", key: "gain_db", label: "Gain", minimum: -18, maximum: 18, step: 0.1, unit: "db", defaultValue: 0 };
const Q: NumberControl = { kind: "number", key: "q", label: "Q", minimum: 0.1, maximum: 18, step: 0.1, unit: "plain", defaultValue: 1 };
const SLOPES = [6, 12, 18, 24];

type Props = {
  block: PresetBlock;
  onEqBand(blockId: string, index: number, patch: Partial<EqBand>, gesture?: string): void;
  onParam(blockId: string, key: string, value: unknown, gesture?: string): void;
};

export function EqEditor({ block, onEqBand, onParam }: Props) {
  const [activeStage, setActiveStage] = useState(1);
  const { bands, highPass, lowPass } = eqStateFor(block);
  const isPass = activeStage === 0 || activeStage === 6;
  const activeFilter = activeStage === 0 ? highPass : lowPass;
  const activeBand = Math.max(0, Math.min(4, activeStage - 1));
  const band = bands[activeBand];
  const filterKey = activeStage === 0 ? "high_pass" : "low_pass";
  const filterName = activeStage === 0 ? "High-pass" : "Low-pass";
  const updateFilter = (patch: Partial<EqPassFilter>, gesture?: string) => onParam(block.id, filterKey, { ...activeFilter, ...patch }, gesture);
  const scale = (control: NumberControl, value: number, onChange: (value: number, gesture: string) => void) => (
    <TravelScale control={control} value={value} family="util" onChange={onChange} />
  );
  return <div className="eq-controls lb-ctl--wide">
    <EqResponseGraph
      bands={bands} highPass={highPass} lowPass={lowPass} activeStage={activeStage} onActiveStage={setActiveStage}
      onBandChange={(index, patch, gesture) => onEqBand(block.id, index, patch, gesture)}
      onPassFilterChange={(key, patch, gesture) => onParam(block.id, key, { ...(key === "high_pass" ? highPass : lowPass), ...patch }, gesture)}
    />
    {isPass ? <fieldset className="eq-band eq-filter">
      <legend>{filterName} filter</legend>
      <Toggle label={`${filterName} enabled`} checked={activeFilter.enabled} onChange={(enabled) => updateFilter({ enabled })} />
      {scale({ ...FREQUENCY, label: "Cutoff" }, activeFilter.frequency_hz, (value, gesture) => updateFilter({ frequency_hz: value }, gesture))}
      {activeFilter.slope_db_per_octave !== 6 && scale({ ...Q, label: "Resonance" }, activeFilter.q, (value, gesture) => updateFilter({ q: value }, gesture))}
      <div className="eq-filter__slope" role="group" aria-label={`${filterName} slope`}>
        {SLOPES.map((slope) => <button type="button" key={slope} aria-pressed={activeFilter.slope_db_per_octave === slope} onClick={() => updateFilter({ slope_db_per_octave: slope })}>{slope} dB/oct</button>)}
      </div>
    </fieldset> : <fieldset className="eq-band">
      <legend>Band {activeBand + 1}</legend>
      <Toggle label={`Band ${activeBand + 1} enabled`} checked={band.enabled} onChange={(enabled) => onEqBand(block.id, activeBand, { enabled })} />
      {scale(FREQUENCY, band.frequency_hz, (value, gesture) => onEqBand(block.id, activeBand, { frequency_hz: value }, gesture))}
      {scale(GAIN, band.gain_db, (value, gesture) => onEqBand(block.id, activeBand, { gain_db: value }, gesture))}
      {scale(Q, band.q, (value, gesture) => onEqBand(block.id, activeBand, { q: value }, gesture))}
    </fieldset>}
  </div>;
}
