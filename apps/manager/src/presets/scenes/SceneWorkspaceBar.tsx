import { ArrowLeftRight, Copy, GitCompareArrows, Radio, Settings2 } from "lucide-react";
import { useEffect, useRef, useState, type KeyboardEvent } from "react";

import type { PresetSceneSet } from "../../api/types";
import { Button, StatusBadge } from "../../components/ui";

type Props = {
  sceneSet: PresetSceneSet;
  editingSceneId: string;
  liveSceneId?: string;
  recallDisabled: boolean;
  recallHint: string;
  recalling: boolean;
  onSelect(sceneId: string): void;
  onRecall(): void;
  onName(sceneId: string, value: string): void;
  onEnterTime(sceneId: string, value: number): void;
  onTrim(sceneId: string, value: number): void;
  onDefault(sceneId: string): void;
  onOpenIn(value: "presets" | "scenes"): void;
  onCopy(sourceSceneId: string, destinationSceneId: string): void;
  onSwap(firstSceneId: string, secondSceneId: string): void;
  sharedRows?: SharedComparisonRow[];
  presentTarget?(target: PresetSceneSet["scenes"][number]["targets"][number], value: number | boolean): { label: string; value: string };
  onCopyRow(rowKey: string, sourceSceneId: string): void;
};

type ComparisonRow = { key: string; label: string; values: string[] };
export type SharedComparisonRow = { key: string; label: string; value: string };

function targetKey(target: PresetSceneSet["scenes"][number]["targets"][number]): string {
  if (target.target === "inputGainDb") return "inputGainDb";
  if (target.target === "parameter") return `parameter:${target.blockId}:${target.parameter}`;
  if (target.target === "blockEnabled") return `enabled:${target.blockId}`;
  return `wdw:${target.lane}:${target.parameter}`;
}

function targetLabel(target: PresetSceneSet["scenes"][number]["targets"][number]): string {
  if (target.target === "inputGainDb") return "Input gain";
  if (target.target === "parameter") return `${target.blockId} · ${target.parameter}`;
  if (target.target === "blockEnabled") return `${target.blockId} · enabled`;
  return `${target.lane === "dry" ? "Dry" : "Wet"} lane · ${target.parameter}`;
}

function targetValue(value: number | boolean): string {
  if (typeof value === "boolean") return value ? "On" : "Off";
  return Number.isInteger(value) ? String(value) : String(Number(value.toFixed(3)));
}

export function sceneComparisonRows(sceneSet: PresetSceneSet, presentTarget?: Props["presentTarget"]): ComparisonRow[] {
  const targets = new Map<string, PresetSceneSet["scenes"][number]["targets"][number]>();
  for (const scene of sceneSet.scenes) for (const target of scene.targets) targets.set(targetKey(target), target);
  return [
    { key: "enterTime", label: "Enter time", values: sceneSet.scenes.map((scene) => scene.enterTimeMs === 0 ? "Instant" : `${scene.enterTimeMs / 1000} s`) },
    { key: "trim", label: "Scene trim", values: sceneSet.scenes.map((scene) => `${scene.outputTrimDb > 0 ? "+" : ""}${scene.outputTrimDb} dB`) },
    ...[...targets.entries()].sort(([left], [right]) => left.localeCompare(right)).map(([key, target]) => {
      const fallbackLabel = targetLabel(target);
      return {
        key,
        label: presentTarget?.(target, target.value).label ?? fallbackLabel,
        values: sceneSet.scenes.map((scene) => {
          const value = scene.targets.find((candidate) => targetKey(candidate) === key);
          return value ? presentTarget?.(value, value.value).value ?? targetValue(value.value) : "Shared";
        }),
      };
    }),
  ];
}

export function SceneWorkspaceBar(props: Props) {
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [compareOpen, setCompareOpen] = useState(false);
  const [comparisonView, setComparisonView] = useState<"differences" | "all" | "shared">("differences");
  const [operation, setOperation] = useState<{ kind: "copy" | "swap"; otherSceneId: string }>();
  const operationRef = useRef<HTMLDivElement>(null);
  const selected = props.sceneSet.scenes.find(({ id }) => id === props.editingSceneId)
    ?? props.sceneSet.scenes[0];
  const otherScenes = props.sceneSet.scenes.filter(({ id }) => id !== selected.id);
  const rows = sceneComparisonRows(props.sceneSet, props.presentTarget);
  const visibleRows = comparisonView === "all" ? rows : rows.filter(({ values }) => new Set(values).size > 1);
  useEffect(() => { if (operation) operationRef.current?.focus(); }, [operation]);

  const moveSelection = (event: KeyboardEvent<HTMLButtonElement>, index: number) => {
    if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
    event.preventDefault();
    const direction = event.key === "ArrowRight" ? 1 : -1;
    const next = (index + direction + props.sceneSet.scenes.length) % props.sceneSet.scenes.length;
    props.onSelect(props.sceneSet.scenes[next].id);
    document.getElementById(`scene-tab-${next}`)?.focus();
  };

  return <section className="scene-workspace" aria-label="Scenes">
    <div className="scene-tabs" role="tablist" aria-label="Editing scene">
      {props.sceneSet.scenes.map((scene, index) => {
        const editing = scene.id === selected.id;
        const live = scene.id === props.liveSceneId;
        return <button
          id={`scene-tab-${index}`}
          key={scene.id}
          type="button"
          role="tab"
          aria-selected={editing}
          tabIndex={editing ? 0 : -1}
          className={`scene-tab${editing ? " scene-tab--editing" : ""}${live ? " scene-tab--live" : ""}`}
          onClick={() => props.onSelect(scene.id)}
          onKeyDown={(event) => moveSelection(event, index)}
        >
          <span>{index + 1}</span><strong>{scene.name}</strong>
          {live && <small><Radio size={12} /> Live</small>}
          {editing && <small>Editing</small>}
        </button>;
      })}
    </div>
    <div className="scene-toolbar">
      <div className="scene-identities" aria-live="polite">
        <span>Live: <strong>{props.sceneSet.scenes.find(({ id }) => id === props.liveSceneId)?.name ?? "Unknown"}</strong></span>
        <span>Editing: <strong>{selected.name}</strong></span>
      </div>
      <div className="scene-toolbar__actions">
        <Button onClick={props.onRecall} disabled={props.recallDisabled || props.recalling} title={props.recallDisabled ? props.recallHint : undefined}>
          <Radio size={15} /> {props.recalling ? "Recalling…" : "Recall on pedal"}
        </Button>
        <Button aria-expanded={settingsOpen} onClick={() => setSettingsOpen((value) => !value)}><Settings2 size={15} /> Scene settings</Button>
        <Button aria-expanded={compareOpen} onClick={() => setCompareOpen((value) => !value)}><GitCompareArrows size={15} /> Compare</Button>
      </div>
      {props.recallDisabled && <small className="scene-recall-hint">{props.recallHint}</small>}
    </div>
    {settingsOpen && <div className="scene-settings" aria-label={`${selected.name} scene settings`}>
      <label>Name<input value={selected.name} maxLength={24} onChange={(event) => props.onName(selected.id, event.target.value)} /></label>
      <label>Enter time (ms)<input type="number" min={0} max={10000} step={10} value={selected.enterTimeMs} onChange={(event) => props.onEnterTime(selected.id, Number(event.target.value))} /></label>
      <label>Scene trim (dB)<input type="number" min={-12} max={6} step={0.1} value={selected.outputTrimDb} onChange={(event) => props.onTrim(selected.id, Number(event.target.value))} /></label>
      <label>Open preset in<select value={props.sceneSet.openIn} onChange={(event) => props.onOpenIn(event.target.value as "presets" | "scenes")}><option value="presets">Presets</option><option value="scenes">Scenes</option></select></label>
      <Button variant="quiet" disabled={props.sceneSet.defaultSceneId === selected.id} onClick={() => props.onDefault(selected.id)}>Make default</Button>
      {props.sceneSet.defaultSceneId === selected.id && <StatusBadge tone="info">Default scene</StatusBadge>}
      <Button variant="quiet" onClick={() => setOperation({ kind: "copy", otherSceneId: otherScenes[0].id })}><Copy size={14} /> Copy to…</Button>
      <Button variant="quiet" onClick={() => setOperation({ kind: "swap", otherSceneId: otherScenes[0].id })}><ArrowLeftRight size={14} /> Swap slots…</Button>
    </div>}
    {compareOpen && <div className="scene-compare" role="region" aria-label="Scene comparison">
      <div className="scene-compare__toolbar"><span>{comparisonView === "differences" ? "Differences only" : comparisonView === "all" ? "All scene-owned settings" : "Shared settings"}</span><div role="group" aria-label="Comparison view">{(["differences", "all", "shared"] as const).map((view) => <button type="button" key={view} aria-pressed={comparisonView === view} onClick={() => setComparisonView(view)}>{view === "differences" ? "Differences" : view === "all" ? "Show all" : "Shared"}</button>)}</div></div>
      {comparisonView !== "shared" ? <table><thead><tr><th>Setting</th>{props.sceneSet.scenes.map((scene) => <th key={scene.id}>{scene.name}</th>)}<th>Action</th></tr></thead>
      <tbody>
        {visibleRows.map((row) => <tr key={row.key}><th>{row.label}</th>{row.values.map((value, index) => <td data-scene={props.sceneSet.scenes[index].name} key={props.sceneSet.scenes[index].id}>{value}</td>)}<td className="scene-compare__action"><Button variant="quiet" onClick={() => props.onCopyRow(row.key, selected.id)}>Copy {selected.name} to all</Button></td></tr>)}
      </tbody></table> : <table className="scene-compare__shared"><thead><tr><th>Setting</th><th>Shared value</th></tr></thead><tbody>{(props.sharedRows ?? []).map((row) => <tr key={row.key}><th>{row.label}</th><td>{row.value}</td></tr>)}</tbody></table>}
      {comparisonView !== "shared" && visibleRows.length === 0 && <p className="scene-compare__empty">These scenes currently use the same values.</p>}
      {comparisonView === "shared" && (props.sharedRows?.length ?? 0) === 0 && <p className="scene-compare__empty">No shared settings are available.</p>}
    </div>}
    {operation && <div ref={operationRef} tabIndex={-1} onKeyDown={(event) => { if (event.key === "Escape") setOperation(undefined); }} className="scene-operation-confirm" role="alertdialog" aria-modal="true" aria-labelledby="scene-operation-title">
      <div><strong id="scene-operation-title">{operation.kind === "copy" ? `Copy ${selected.name}` : `Swap ${selected.name}`}</strong><p>{operation.kind === "copy" ? "The destination keeps its name and identity. Its values, timing, and trim will be replaced." : "Scene identities, names, MIDI references, and default status move with their slots."}</p></div>
      <label>{operation.kind === "copy" ? "Destination" : "Other scene"}<select value={operation.otherSceneId} onChange={(event) => setOperation({ ...operation, otherSceneId: event.target.value })}>{otherScenes.map((scene) => <option key={scene.id} value={scene.id}>{scene.name}</option>)}</select></label>
      <div><Button variant="quiet" onClick={() => setOperation(undefined)}>Cancel</Button><Button variant="primary" onClick={() => { if (operation.kind === "copy") props.onCopy(selected.id, operation.otherSceneId); else props.onSwap(selected.id, operation.otherSceneId); setOperation(undefined); }}>Confirm</Button></div>
    </div>}
  </section>;
}
