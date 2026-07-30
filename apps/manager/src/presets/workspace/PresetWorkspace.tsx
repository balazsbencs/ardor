import { AlertCircle, Check, CloudOff, Redo2, Save, Send, SlidersHorizontal, Undo2 } from "lucide-react";
import { useEffect, useMemo, useReducer, useState } from "react";

import { Button, IconButton, StatusBadge } from "../../components/ui";
import { useDeviceSession } from "../../connection/deviceSession";
import { allEffectDefinitions, findEffectDefinition } from "../../effects/catalog";
import { PresetSidebar } from "../browser/PresetSidebar";
import { BlockBrowser } from "../block-browser/BlockBrowser";
import { ChainCanvas } from "../chain/ChainCanvas";
import { createEditorState, editorReducer, findPresetBlock, isEditorDirty } from "../editor/editorReducer";
import type { PresetLocation } from "../editor/editorTypes";
import { validatePreset, issuesForBlock } from "../editor/presetValidation";
import { BlockInspector } from "../inspector/BlockInspector";
import { UnsavedChangesDialog } from "./UnsavedChangesDialog";

export function PresetWorkspace({ onAssets, onConnection }: { onAssets(): void; onConnection(): void }) {
  const session = useDeviceSession();
  const [editor, dispatch] = useReducer(editorReducer, undefined, () => createEditorState({ bank: 0, slot: 0 }, {
    version: 1, name: "New Preset", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [],
  }));
  const [addTarget, setAddTarget] = useState<
    | { kind: "top"; index: number }
    | { kind: "lane"; rigId: string; lane: "left" | "right"; index: number }
  >();
  const [pendingLocation, setPendingLocation] = useState<PresetLocation>();
  const [actionError, setActionError] = useState<string>();
  const [applied, setApplied] = useState<PresetLocation>();
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    if (!session.current) return;
    dispatch({ type: "load", location: session.current.location, preset: session.current.preset });
  }, [session.current?.location.bank, session.current?.location.slot, session.current?.preset]);

  const present = editor.history.present;
  const validation = useMemo(() => validatePreset(present, { models: session.models, irs: session.irs }), [present, session.models, session.irs]);
  const dirty = isEditorDirty(editor);
  const selected = findPresetBlock(present.blocks, editor.selectedBlockId);
  const disabledDefinitions = useMemo(() => {
    const result = new Map<string, string>();
    if (addTarget?.kind === "lane") {
      result.set("dualAmp", "Split blocks cannot be nested");
      result.set("dualRig", "Split blocks cannot be nested");
      const rig = findPresetBlock(present.blocks, addTarget.rigId);
      const blocks = rig?.lanes?.[addTarget.lane].blocks ?? [];
      for (const definition of allEffectDefinitions()) {
        if (!definition.constraintGroup || definition.maxEnabledInGroup !== 1) continue;
        const conflict = blocks.find((block) => block.enabled
          && findEffectDefinition(block)?.constraintGroup === definition.constraintGroup);
        if (conflict) result.set(definition.id, `Disable ${findEffectDefinition(conflict)?.name ?? conflict.type} first`);
      }
      return result;
    }
    const enabledParallelRig = present.blocks.find((block) => block.enabled
      && (block.type === "dualAmp" || block.type === "dualRig"));
    const enabledStandaloneAmp = present.blocks.find((block) => block.enabled && (block.type === "nam" || block.type === "cab"));
    for (const definition of allEffectDefinitions()) {
      if ((definition.id === "dualAmp" || definition.id === "dualRig") && enabledParallelRig) {
        result.set(definition.id, `Disable the existing ${findEffectDefinition(enabledParallelRig)?.name ?? "parallel rig"} first`);
        continue;
      }
      if ((definition.id === "dualAmp" || definition.id === "dualRig") && enabledStandaloneAmp) {
        result.set(definition.id, `Disable ${findEffectDefinition(enabledStandaloneAmp)?.name ?? enabledStandaloneAmp.type} first`);
        continue;
      }
      if ((definition.blockType === "nam" || definition.blockType === "cab") && enabledParallelRig) {
        result.set(definition.id, `Disable ${findEffectDefinition(enabledParallelRig)?.name ?? "the parallel rig"} first`);
        continue;
      }
      if (!definition.constraintGroup || definition.maxEnabledInGroup !== 1) continue;
      const conflict = present.blocks.find((block) => block.enabled && findEffectDefinition(block)?.constraintGroup === definition.constraintGroup);
      if (conflict) result.set(definition.id, `Disable ${findEffectDefinition(conflict)?.name ?? conflict.type} first`);
    }
    return result;
  }, [addTarget, present.blocks]);

  const selectLocation = (location: PresetLocation) => {
    if (location.bank === editor.location.bank && location.slot === editor.location.slot) return;
    if (dirty) {
      setPendingLocation(location);
      return;
    }
    void session.selectLocation(location).catch((reason) => setActionError(reason instanceof Error ? reason.message : "Could not load preset."));
  };

  const save = async (): Promise<boolean> => {
    if (!validation.canSave) return false;
    setActionError(undefined);
    setSaving(true);
    try {
      const response = await session.saveCurrent(present);
      if (!response) return false;
      dispatch({ type: "mark-saved", preset: response.preset });
      await session.refreshPresets();
      return true;
    } catch (reason) {
      setActionError(reason instanceof Error ? reason.message : "Could not save preset.");
      return false;
    } finally {
      setSaving(false);
    }
  };

  const apply = async () => {
    if (!validation.canApply) return;
    setActionError(undefined);
    try {
      const response = await session.applyCurrent();
      if (response?.accepted) setApplied(editor.location);
    } catch (reason) {
      setActionError(reason instanceof Error ? reason.message : "Could not apply preset.");
    }
  };

  const saveAndApply = async () => { if (await save()) await apply(); };

  const resolveNavigation = async (choice: "save" | "discard" | "cancel") => {
    const destination = pendingLocation;
    setPendingLocation(undefined);
    if (!destination || choice === "cancel") return;
    if (choice === "save" && !(await save())) return;
    if (choice === "discard") dispatch({ type: "load", location: editor.location, preset: editor.saved });
    await session.selectLocation(destination);
  };

  if (session.status !== "connected" || !session.current) {
    return <main className="workspace workspace--offline"><div className="offline-card"><CloudOff size={38} /><p className="eyebrow">Ardor Manager</p><h1>Connect to your pedal</h1><p>Manage preset chains, models and cabinet IRs from one desktop workspace.</p><Button variant="primary" onClick={onConnection}>Connect to device</Button></div></main>;
  }

  const locationLabel = `Bank ${String(editor.location.bank).padStart(3, "0")} / Slot ${editor.location.slot + 1}`;
  const applyBlocked = !validation.canApply || dirty || session.busy.apply || saving;
  return (
    <main className="workspace">
      <PresetSidebar summaries={session.presets} selected={editor.location} disabled={saving || session.busy.apply} onSelect={selectLocation} />
      <section className="workspace-main">
        <header className="preset-header">
          <div><p className="eyebrow">{locationLabel}</p><input aria-label="Preset name" className="preset-name-input" value={present.name} onChange={(event) => dispatch({ type: "set-name", name: event.target.value })} /><div className="preset-header__meta">{dirty && <StatusBadge tone="warning">Unsaved changes</StatusBadge>}{applied?.bank === editor.location.bank && applied.slot === editor.location.slot && <StatusBadge tone="success"><Check size={13} /> Applied this session</StatusBadge>}</div></div>
          <div className="preset-actions"><IconButton label="Undo" disabled={editor.history.past.length === 0} onClick={() => dispatch({ type: "undo" })}><Undo2 size={17} /></IconButton><IconButton label="Redo" disabled={editor.history.future.length === 0} onClick={() => dispatch({ type: "redo" })}><Redo2 size={17} /></IconButton><Button variant="secondary" disabled={!dirty || !validation.canSave || saving} onClick={() => void save()}><Save size={16} /> {saving ? "Saving…" : "Save"}</Button><Button variant="primary" disabled={!validation.canApply || saving || session.busy.apply} onClick={() => void saveAndApply()}><Send size={16} /> Save & Apply</Button><Button variant="quiet" disabled={applyBlocked} onClick={() => void apply()}>Apply</Button></div>
        </header>
        <div className="global-strip"><SlidersHorizontal size={17} /><label>Input<input type="number" min={-60} max={24} value={present.global.inputGainDb} onChange={(event) => dispatch({ type: "set-global", key: "inputGainDb", value: Number(event.target.value) })} /><small>dB</small></label><label>Output<input type="number" min={-60} max={24} value={present.global.outputGainDb} onChange={(event) => dispatch({ type: "set-global", key: "outputGainDb", value: Number(event.target.value) })} /><small>dB</small></label><span>Serial routing</span></div>
        {(!validation.canSave || validation.issues.length > 0 || actionError) && <div className="workspace-alert" role="alert"><AlertCircle size={17} /><div>{actionError ? <p>{actionError}</p> : <p>{validation.issues[0]?.message}</p>}<small>{!validation.canSave ? "Fix this before saving." : !validation.canApply ? "You can save this draft, but cannot apply it yet." : "Review the highlighted block."}</small></div></div>}
        <ChainCanvas blocks={present.blocks} selectedBlockId={editor.selectedBlockId} issuesFor={(id) => issuesForBlock(validation, id)} maxed={present.blocks.length >= 10} onSelect={(blockId) => dispatch({ type: "select-block", blockId })} onAdd={(index) => setAddTarget({ kind: "top", index })} onMove={(blockId, index) => dispatch({ type: "move-block", blockId, index })} onLaneAdd={(rigId, lane, index) => setAddTarget({ kind: "lane", rigId, lane, index })} onLaneMove={(rigId, blockId, lane, index) => dispatch({ type: "move-lane-block", rigId, blockId, lane, index })} onToggle={(blockId, enabled) => dispatch({ type: "toggle-block", blockId, enabled })} onDuplicate={(blockId) => dispatch({ type: "duplicate-block", blockId })} onReset={(blockId) => dispatch({ type: "reset-block", blockId })} onDelete={(blockId) => dispatch({ type: "remove-block", blockId })} />
      </section>
      <BlockInspector block={selected} issues={selected ? issuesForBlock(validation, selected.id) : []} models={session.models} irs={session.irs} onToggle={(blockId, enabled) => dispatch({ type: "toggle-block", blockId, enabled })} onParam={(blockId, key, value) => dispatch({ type: "set-block-param", blockId, key, value })} onAsset={(blockId, asset) => dispatch({ type: "set-block-asset", blockId, asset })} onMode={(blockId, definitionId) => dispatch({ type: "change-definition", blockId, definitionId })} onEqBand={(blockId, band, patch) => dispatch({ type: "set-eq-band", blockId, band, patch })} onReset={(blockId) => dispatch({ type: "reset-block", blockId })} onDuplicate={(blockId) => dispatch({ type: "duplicate-block", blockId })} onDelete={(blockId) => dispatch({ type: "remove-block", blockId })} onAssets={onAssets} />
      <BlockBrowser open={addTarget !== undefined} onOpenChange={(open) => { if (!open) setAddTarget(undefined); }} disabledIds={disabledDefinitions} onChoose={(definition) => {
        if (addTarget?.kind === "lane") {
          dispatch({ type: "add-lane-block", definitionId: definition.id, rigId: addTarget.rigId, lane: addTarget.lane, index: addTarget.index });
        } else {
          dispatch({ type: "add-block", definitionId: definition.id, index: addTarget?.index ?? present.blocks.length });
        }
        setAddTarget(undefined);
      }} />
      <UnsavedChangesDialog open={pendingLocation !== undefined} busy={saving} onChoice={(choice) => void resolveNavigation(choice)} />
    </main>
  );
}
