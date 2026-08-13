import { AlertTriangle, Copy, RotateCcw, Trash2, X } from "lucide-react";
import { useState } from "react";

import type { Asset, PresetBlock } from "../../api/types";
import { allEffectDefinitions, findEffectDefinition } from "../../effects/catalog";
import type { ChoiceControl, EffectDefinition, ToggleControl } from "../../effects/types";
import { ParameterSlider } from "../../components/ParameterSlider";
import { Button, IconButton, StatusBadge, Toggle } from "../../components/ui";
import type { EqBand, EqPassFilter } from "../editor/editorTypes";
import type { ValidationIssue } from "../editor/presetValidation";
import { EqResponseGraph } from "./EqResponseGraph";

function valueFor(block: PresetBlock, key: string, fallback: number | string | boolean): number | string | boolean {
  const value = block.params[key];
  if (key === "useNano" && value === undefined && typeof block.params.quality === "number") {
    return block.params.quality === 0;
  }
  return typeof value === typeof fallback ? value as typeof fallback : fallback;
}

export function BlockInspector({
  block,
  issues,
  models,
  irs,
  onToggle,
  onParam,
  onAsset,
  onMode,
  onEqBand,
  onReset,
  onDuplicate,
  onDelete,
  onAssets,
  onClose,
}: {
  block?: PresetBlock;
  issues: ValidationIssue[];
  models: Asset[];
  irs: Asset[];
  onToggle(blockId: string, enabled: boolean): void;
  onParam(blockId: string, key: string, value: unknown): void;
  onAsset(blockId: string, asset: string): void;
  onMode(blockId: string, definitionId: string): void;
  onEqBand(blockId: string, index: number, patch: Partial<EqBand>): void;
  onReset(blockId: string): void;
  onDuplicate(blockId: string): void;
  onDelete(blockId: string): void;
  onClose?(): void;
  onAssets(): void;
}) {
  if (!block) return <aside className="inspector inspector--empty"><div><p className="eyebrow">Inspector</p><h2>Select a block</h2><p>Choose a block in the chain to edit its sound, routing state and asset.</p></div></aside>;
  const definition = findEffectDefinition(block);
  if (!definition) return <UnknownInspector block={block} issues={issues} onToggle={onToggle} onDelete={onDelete} onClose={onClose} />;
  const modes = definition.mode ? allModesFor(definition) : [];
  const renderControl = (control: EffectDefinition["controls"][number]) => {
    if (control.kind === "asset") {
      const value = control.key ? String(valueFor(block, control.key, "")) : block.asset;
      return <AssetPicker key={control.key ?? control.label} value={value} label={control.label} assets={control.assetKind === "models" ? models : irs} onChange={(asset) => control.key ? onParam(block.id, control.key, asset) : onAsset(block.id, asset)} onAssets={onAssets} />;
    }
    if (control.kind === "number") return <ParameterSlider key={control.key} control={control} value={Number(valueFor(block, control.key, control.defaultValue))} onChange={(value) => onParam(block.id, control.key, value)} />;
    if (control.kind === "choice") return <ChoiceField key={control.key} control={control} value={String(valueFor(block, control.key, control.defaultValue))} onChange={(value) => onParam(block.id, control.key, value)} />;
    if (control.kind === "toggle") return <ToggleField key={control.key} control={control} value={Boolean(valueFor(block, control.key, control.defaultValue))} onChange={(value) => onParam(block.id, control.key, value)} />;
    return <EqControls key="eq" block={block} onEqBand={onEqBand} onParam={onParam} />;
  };
  const controlKey = (control: EffectDefinition["controls"][number]) =>
    "key" in control && typeof control.key === "string" ? control.key : "";
  const generalControls = definition.controls.filter((control) => {
    const key = controlKey(control);
    return !key.startsWith("left") && !key.startsWith("right");
  });
  const leftControls = definition.controls.filter((control) => controlKey(control).startsWith("left"));
  const rightControls = definition.controls.filter((control) => controlKey(control).startsWith("right"));
  return (
    <aside className="inspector" aria-label={`${definition.name} inspector`}>
      <div className="inspector__heading"><div><p className="eyebrow">{definition.category}</p><h2>{definition.name}</h2><span className="inspector__id">{block.id}</span></div><div className="inspector__heading-actions"><Toggle label={`${definition.name} enabled`} checked={block.enabled} onChange={(enabled) => onToggle(block.id, enabled)} />{onClose && <IconButton className="inspector__close" label="Close inspector" onClick={onClose}><X size={16} /></IconButton>}</div></div>
      {issues.length > 0 && <div className="inspector-issues">{issues.map((issue, index) => <p key={`${issue.code}-${index}`}><AlertTriangle size={15} /><span>{issue.message}</span></p>)}</div>}
      {modes.length > 1 && <label className="form-field"><span>Mode</span><select aria-label="Effect mode" value={definition.id} onChange={(event) => onMode(block.id, event.target.value)}>{modes.map((mode) => <option value={mode.id} key={mode.id}>{mode.name}</option>)}</select></label>}
      <div className="inspector__controls">
        {(block.type === "dualAmp" || block.type === "dualRig") ? <>
          {generalControls.map(renderControl)}
          <div className="dual-amp-controls">
            <fieldset><legend><span>L</span> Left lane</legend>{leftControls.map(renderControl)}</fieldset>
            <fieldset><legend><span>R</span> Right lane</legend>{rightControls.map(renderControl)}</fieldset>
          </div>
        </> : definition.controls.map(renderControl)}
      </div>
      <div className="inspector__footer"><Button variant="quiet" onClick={() => onReset(block.id)}><RotateCcw size={15} /> Reset</Button><Button variant="quiet" disabled={block.type === "dualAmp" || block.type === "dualRig"} onClick={() => onDuplicate(block.id)}><Copy size={15} /> Duplicate</Button><Button variant="danger" onClick={() => onDelete(block.id)}><Trash2 size={15} /> Delete</Button></div>
    </aside>
  );
}

function allModesFor(definition: EffectDefinition): EffectDefinition[] {
  return allEffectDefinitions().filter((candidate) => candidate.blockType === definition.blockType && candidate.mode);
}

function ChoiceField({ control, value, onChange }: { control: ChoiceControl; value: string; onChange(value: string): void }) {
  return <label className="form-field"><span>{control.label}</span><select value={value} onChange={(event) => onChange(event.target.value)}>{control.choices.map((choice) => <option value={choice.value} key={choice.value}>{choice.label}</option>)}</select></label>;
}

function ToggleField({ control, value, onChange }: { control: ToggleControl; value: boolean; onChange(value: boolean): void }) {
  return <div className="toggle-field"><span>{control.label}</span><Toggle label={control.label} checked={value} onChange={onChange} /></div>;
}

function AssetPicker({
  value,
  label,
  assets,
  onChange,
  onAssets,
}: {
  value: string;
  label: string;
  assets: Asset[];
  onChange(asset: string): void;
  onAssets(): void;
}) {
  const missing = value.length > 0 && !assets.some((asset) => asset.path === value);
  return <div className="asset-picker"><label className="form-field"><span>{label}</span><select value={value} onChange={(event) => onChange(event.target.value)}><option value="">Choose an asset…</option>{missing && <option value={value}>{value} (missing)</option>}{assets.map((asset) => <option value={asset.path} key={asset.id}>{asset.filename}</option>)}</select></label>{missing && <p className="asset-picker__missing"><StatusBadge tone="warning">Missing</StatusBadge> This file is not installed.</p>}<Button variant="quiet" onClick={onAssets}>Manage assets</Button></div>;
}

function EqControls({
  block,
  onEqBand,
  onParam,
}: {
  block: PresetBlock;
  onEqBand(blockId: string, index: number, patch: Partial<EqBand>): void;
  onParam(blockId: string, key: string, value: unknown): void;
}) {
  const [activeStage, setActiveStage] = useState(1);
  const sourceBands = Array.isArray(block.params.bands) ? block.params.bands as EqBand[] : [];
  const bands = [0, 1, 2, 3, 4].map((index) => sourceBands[index] ?? { enabled: true, frequency_hz: [80, 250, 800, 2500, 8000][index], q: 1, gain_db: 0 });
  const filterFrom = (key: "high_pass" | "low_pass", fallbackFrequency: number): EqPassFilter => {
    const source = block.params[key];
    return typeof source === "object" && source !== null && !Array.isArray(source)
      ? { enabled: false, frequency_hz: fallbackFrequency, q: 0.70710678, slope_db_per_octave: 12, ...source } as EqPassFilter
      : { enabled: false, frequency_hz: fallbackFrequency, q: 0.70710678, slope_db_per_octave: 12 };
  };
  const highPass = filterFrom("high_pass", 40);
  const lowPass = filterFrom("low_pass", 16000);
  const isPass = activeStage === 0 || activeStage === 6;
  const activeFilter = activeStage === 0 ? highPass : lowPass;
  const activeBand = Math.max(0, Math.min(4, activeStage - 1));
  const band = bands[activeBand];
  const filterKey = activeStage === 0 ? "high_pass" : "low_pass";
  const updateFilter = (patch: Partial<EqPassFilter>) => onParam(
    block.id, filterKey, { ...activeFilter, ...patch },
  );
  return <div className="eq-controls">
    <EqResponseGraph
      bands={bands}
      highPass={highPass}
      lowPass={lowPass}
      activeStage={activeStage}
      onActiveStage={setActiveStage}
      onBandChange={(index, patch) => onEqBand(block.id, index, patch)}
      onPassFilterChange={(key, patch) => onParam(block.id, key, {
        ...(key === "high_pass" ? highPass : lowPass), ...patch,
      })}
    />
    {isPass ? <fieldset className="eq-band eq-filter">
      <legend>{activeStage === 0 ? "High-pass filter" : "Low-pass filter"}</legend>
      <Toggle label={`${activeStage === 0 ? "High-pass" : "Low-pass"} enabled`} checked={activeFilter.enabled} onChange={(enabled) => updateFilter({ enabled })} />
      <label>Cutoff<input aria-label={`${activeStage === 0 ? "High-pass" : "Low-pass"} cutoff`} type="number" min={20} max={20000} value={activeFilter.frequency_hz} onChange={(event) => updateFilter({ frequency_hz: Number(event.target.value) })} /><small>Hz</small></label>
      <label>Resonance<input aria-label={`${activeStage === 0 ? "High-pass" : "Low-pass"} resonance`} type="number" min={0.1} max={18} step={0.1} value={activeFilter.q} disabled={activeFilter.slope_db_per_octave === 6} title={activeFilter.slope_db_per_octave === 6 ? "Resonance applies from 12 dB/oct" : undefined} onChange={(event) => updateFilter({ q: Number(event.target.value) })} /><small>12+ only</small></label>
      <div className="eq-filter__slope"><span>Slope</span><div className="eq-slope-options" role="group" aria-label={`${activeStage === 0 ? "High-pass" : "Low-pass"} slope`}>{[6, 12, 18, 24].map((slope) => <button type="button" key={slope} aria-pressed={activeFilter.slope_db_per_octave === slope} onClick={() => updateFilter({ slope_db_per_octave: slope })}>{slope}<small>dB/oct</small></button>)}</div></div>
    </fieldset> : <fieldset className="eq-band">
      <legend>Band {activeBand + 1}</legend>
      <Toggle label={`Band ${activeBand + 1} enabled`} checked={band.enabled} onChange={(enabled) => onEqBand(block.id, activeBand, { enabled })} />
      <label>Frequency<input aria-label="Frequency" type="number" min={20} max={20000} value={band.frequency_hz} onChange={(event) => onEqBand(block.id, activeBand, { frequency_hz: Number(event.target.value) })} /><small>Hz</small></label>
      <label>Gain<input aria-label="Gain" type="number" min={-18} max={18} step={0.5} value={band.gain_db} onChange={(event) => onEqBand(block.id, activeBand, { gain_db: Number(event.target.value) })} /><small>dB</small></label>
      <label>Q<input aria-label="Q" type="number" min={0.1} max={18} step={0.1} value={band.q} onChange={(event) => onEqBand(block.id, activeBand, { q: Number(event.target.value) })} /></label>
    </fieldset>}
  </div>;
}

function UnknownInspector({ block, issues, onToggle, onDelete, onClose }: { block: PresetBlock; issues: ValidationIssue[]; onToggle(blockId: string, enabled: boolean): void; onDelete(blockId: string): void; onClose?(): void }) {
  return <aside className="inspector"><div className="inspector__heading"><div><p className="eyebrow">Unsupported block</p><h2>{block.type}</h2><span className="inspector__id">{block.id}</span></div><div className="inspector__heading-actions"><Toggle label={`${block.type} enabled`} checked={block.enabled} onChange={(enabled) => onToggle(block.id, enabled)} />{onClose && <IconButton className="inspector__close" label="Close inspector" onClick={onClose}><X size={16} /></IconButton>}</div></div><div className="inspector-issues">{issues.map((issue, index) => <p key={`${issue.code}-${index}`}><AlertTriangle size={15} /><span>{issue.message}</span></p>)}</div><p className="unknown-copy">This manager preserves the block and its parameters. It cannot safely expose controls for this type.</p><pre>{JSON.stringify(block.params, null, 2)}</pre><div className="inspector__footer"><Button variant="danger" onClick={() => onDelete(block.id)}><Trash2 size={15} /> Delete</Button></div></aside>;
}
