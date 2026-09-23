import { AlertCircle, Check, CloudOff, Redo2, Save, Send, SlidersHorizontal, Undo2 } from "lucide-react";
import { useEffect, useMemo, useReducer, useState } from "react";

import { Button, IconButton, StatusBadge } from "../../components/ui";
import { displayValue } from "../../components/ParameterSlider";
import { useDeviceSession } from "../../connection/deviceSession";
import type { PresetBlock, PresetSceneTarget, WdwRouting } from "../../api/types";
import { allEffectDefinitions, findEffectDefinition } from "../../effects/catalog";
import { PresetSidebar } from "../browser/PresetSidebar";
import { BlockBrowser } from "../block-browser/BlockBrowser";
import { ChainCanvas } from "../chain/ChainCanvas";
import { allPresetBlocksInPreset, createEditorState, editorReducer, findPresetBlockInPreset, isEditorDirty } from "../editor/editorReducer";
import type { PresetLocation } from "../editor/editorTypes";
import { validatePreset, issuesForBlock } from "../editor/presetValidation";
import { isWdwBlockAllowed, wdwLaneLabel } from "../editor/wdwPolicy";
import { BlockInspector } from "../inspector/BlockInspector";
import { UnsavedChangesDialog } from "./UnsavedChangesDialog";
import { WdwRoutingCanvas } from "../chain/WdwRoutingCanvas";
import { SceneWorkspaceBar, type SharedComparisonRow } from "../scenes/SceneWorkspaceBar";
import { activeRevisionMatchesDraft } from "../scenes/sceneRevision";

function blocksForScene(blocks: PresetBlock[], enabledById: Map<string, boolean>): PresetBlock[] {
  return blocks.map((block) => ({
    ...block,
    enabled: enabledById.get(block.id) ?? block.enabled,
    ...(block.lanes ? { lanes: {
      left: { blocks: blocksForScene(block.lanes.left.blocks, enabledById) },
      right: { blocks: blocksForScene(block.lanes.right.blocks, enabledById) },
    } } : {}),
  }));
}

export function PresetWorkspace({ onAssets, onConnection }: { onAssets(): void; onConnection(): void }) {
  const session = useDeviceSession();
  const [editor, dispatch] = useReducer(editorReducer, undefined, () => createEditorState({ bank: 0, slot: 0 }, {
    version: 1, name: "New Preset", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [],
  }));
  const [addTarget, setAddTarget] = useState<
    | { kind: "top"; index: number }
    | { kind: "lane"; rigId: string; lane: "left" | "right"; index: number }
    | { kind: "wdw"; lane: "dry" | "wet"; index: number }
  >();
  const [pendingLocation, setPendingLocation] = useState<PresetLocation>();
  const [actionError, setActionError] = useState<string>();
  const [applied, setApplied] = useState<PresetLocation>();
  const [recallingScene, setRecallingScene] = useState(false);
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    if (!session.current) return;
    dispatch({ type: "load", location: session.current.location, preset: session.current.preset });
  }, [session.current?.location.bank, session.current?.location.slot, session.current?.preset]);

  const present = editor.history.present;
  const validation = useMemo(() => validatePreset(present, {
    models: session.models, irs: session.irs, reverbIrs: session.reverbIrs,
  }), [present, session.models, session.irs, session.reverbIrs]);
  const dirty = isEditorDirty(editor);
  const allBlocks = useMemo(() => allPresetBlocksInPreset(present), [present]);
  const selected = findPresetBlockInPreset(present, editor.selectedBlockId);
  const editingScene = present.sceneSet?.scenes.find(({ id }) => id === editor.editingSceneId);
  const sceneInputGain = editingScene?.targets.find((target) => target.target === "inputGainDb");
  const sceneInputOwned = sceneInputGain?.target === "inputGainDb";
  const displayedInputGain = sceneInputOwned ? sceneInputGain.value : present.global.inputGainDb;
  const enabledById = useMemo(() => new Map(
    editingScene?.targets.flatMap((target) => target.target === "blockEnabled"
      ? [[target.blockId, target.value] as const] : []) ?? [],
  ), [editingScene]);
  const displayedBlocks = useMemo(() => blocksForScene(present.blocks, enabledById), [present.blocks, enabledById]);
  const displayedWdw = useMemo<WdwRouting | undefined>(() => {
    if (!present.wdw) return undefined;
    const routing = structuredClone(present.wdw);
    routing.dry.blocks = blocksForScene(routing.dry.blocks, enabledById);
    routing.wet.blocks = blocksForScene(routing.wet.blocks, enabledById);
    for (const target of editingScene?.targets ?? []) {
      if (target.target !== "wdwLane") continue;
      const lane = routing[target.lane];
      if (target.parameter === "enabled" && typeof target.value === "boolean") lane.enabled = target.value;
      else if (target.parameter === "levelDb" && typeof target.value === "number") lane.levelDb = target.value;
      else if (target.parameter === "pan" && typeof target.value === "number") lane.pan = target.value;
      else if (target.parameter === "width" && typeof target.value === "number") lane.width = target.value;
    }
    return routing;
  }, [present.wdw, editingScene, enabledById]);
  const inspectorBlock = useMemo(() => {
    if (!selected || !editingScene) return selected;
    const block = structuredClone(selected);
    for (const target of editingScene.targets) {
      if (target.target === "parameter" && target.blockId === block.id) block.params[target.parameter] = target.value;
      else if (target.target === "blockEnabled" && target.blockId === block.id) block.enabled = target.value;
    }
    return block;
  }, [selected, editingScene]);
  const sceneScopeFor = (blockId: string, parameter?: string): "shared" | "scene" =>
    editingScene?.targets.some((target) => parameter !== undefined
      ? target.target === "parameter" && target.blockId === blockId && target.parameter === parameter
      : target.target === "blockEnabled" && target.blockId === blockId) ? "scene" : "shared";
  const editBlockEnabled = (blockId: string, enabled: boolean) => {
    if (editingScene && sceneScopeFor(blockId) === "scene") {
      dispatch({ type: "set-scene-block-enabled", sceneId: editingScene.id, blockId, value: enabled });
    } else dispatch({ type: "toggle-block", blockId, enabled });
  };
  const editParameter = (blockId: string, parameter: string, value: unknown) => {
    if (editingScene && typeof value === "number" && sceneScopeFor(blockId, parameter) === "scene") {
      dispatch({ type: "set-scene-parameter", sceneId: editingScene.id, blockId, parameter, value });
    } else dispatch({ type: "set-block-param", blockId, key: parameter, value });
  };
  const editWdwMix = (lane: "dry" | "wet", key: "levelDb" | "pan" | "width" | "enabled", value: number | boolean) => {
    if (editingScene?.targets.some((target) => target.target === "wdwLane"
        && target.lane === lane && target.parameter === key)) {
      dispatch({ type: "set-scene-wdw-mix", sceneId: editingScene.id, lane, key, value });
    } else dispatch({ type: "set-wdw-mix", lane, key, value });
  };
  const expressionTargets = useMemo(() => allBlocks.flatMap((block) => {
    if (!["mod", "delay", "reverb", "dynamics", "cab", "wah"].includes(block.type)) return [];
    const definition = findEffectDefinition(block);
    const parameters = definition?.controls.flatMap((control) =>
      control.kind === "number" ? [control] : [],
    ) ?? [];
    return parameters.length > 0 ? [{
      block,
      name: definition?.name ?? block.type,
      parameters,
    }] : [];
  }), [allBlocks]);
  const sceneTargetAddress = (target: PresetSceneTarget) => {
    if (target.target === "inputGainDb") return "inputGainDb";
    if (target.target === "parameter") return `parameter:${target.blockId}:${target.parameter}`;
    if (target.target === "blockEnabled") return `enabled:${target.blockId}`;
    return `wdw:${target.lane}:${target.parameter}`;
  };
  const sceneOwnedAddresses = useMemo(() => new Set(
    present.sceneSet?.scenes.flatMap((scene) => scene.targets.map(sceneTargetAddress)) ?? [],
  ), [present.sceneSet]);
  const presentSceneTarget = (target: PresetSceneTarget, value: number | boolean) => {
    if (target.target === "inputGainDb") return { label: "Input gain", value: `${Number(value).toFixed(1)} dB` };
    if (target.target === "blockEnabled") {
      const block = allBlocks.find(({ id }) => id === target.blockId);
      const definition = block ? findEffectDefinition(block) : undefined;
      return { label: `${definition?.name ?? target.blockId} · enabled`, value: value ? "On" : "Off" };
    }
    if (target.target === "parameter") {
      const block = allBlocks.find(({ id }) => id === target.blockId);
      const definition = block ? findEffectDefinition(block) : undefined;
      const control = definition?.controls.find((candidate) => candidate.kind === "number" && candidate.key === target.parameter);
      return {
        label: `${definition?.name ?? target.blockId} · ${control?.kind === "number" ? control.label : target.parameter}`,
        value: control?.kind === "number" ? displayValue(control, Number(value)) : String(value),
      };
    }
    const label = `${target.lane === "dry" ? "Dry" : "Wet"} lane · ${target.parameter}`;
    const formatted = target.parameter === "enabled" ? (value ? "On" : "Off")
      : target.parameter === "levelDb" ? `${Number(value).toFixed(1)} dB`
        : target.parameter === "width" ? `${Math.round(Number(value) * 100)}%` : String(value);
    return { label, value: formatted };
  };
  const sharedComparisonRows = useMemo<SharedComparisonRow[]>(() => {
    const rows: SharedComparisonRow[] = [
      { key: "routing", label: "Preset · topology", value: present.routing === "wdw" ? "Wet / dry / wet" : "Serial" },
      { key: "outputGainDb", label: "Preset · output gain", value: `${present.global.outputGainDb.toFixed(1)} dB` },
      { key: "safetyLimitDb", label: "Preset · safety limit", value: `${present.global.safetyLimitDb.toFixed(1)} dB` },
    ];
    if (!sceneOwnedAddresses.has("inputGainDb")) rows.push({ key: "inputGainDb", label: "Preset · input gain", value: `${present.global.inputGainDb.toFixed(1)} dB` });
    for (const block of allBlocks) {
      const definition = findEffectDefinition(block);
      const name = definition?.name ?? block.id;
      if (!sceneOwnedAddresses.has(`enabled:${block.id}`)) rows.push({ key: `enabled:${block.id}`, label: `${name} · enabled`, value: block.enabled ? "On" : "Off" });
      for (const control of definition?.controls ?? []) {
        if (control.kind === "number") {
          if (sceneOwnedAddresses.has(`parameter:${block.id}:${control.key}`)) continue;
          const value = typeof block.params[control.key] === "number" ? Number(block.params[control.key]) : control.defaultValue;
          rows.push({ key: `parameter:${block.id}:${control.key}`, label: `${name} · ${control.label}`, value: displayValue(control, value) });
        } else if (control.kind === "choice") {
          const value = typeof block.params[control.key] === "string" ? String(block.params[control.key]) : control.defaultValue;
          rows.push({ key: `parameter:${block.id}:${control.key}`, label: `${name} · ${control.label}`, value: control.choices.find((choice) => choice.value === value)?.label ?? value });
        } else if (control.kind === "toggle") {
          const value = typeof block.params[control.key] === "boolean" ? Boolean(block.params[control.key]) : control.defaultValue;
          rows.push({ key: `parameter:${block.id}:${control.key}`, label: `${name} · ${control.label}`, value: value ? "On" : "Off" });
        } else if (control.kind === "asset") {
          const value = control.key ? String(block.params[control.key] ?? "") : block.asset;
          const segments = value.split("/");
          rows.push({ key: `asset:${block.id}:${control.key ?? "primary"}`, label: `${name} · ${control.label}`, value: segments[segments.length - 1] || "Not selected" });
        } else {
          rows.push({ key: `equalizer:${block.id}`, label: `${name} · equalizer`, value: "5-band parametric EQ" });
        }
      }
    }
    if (present.wdw) {
      const laneRows = [
        { lane: "dry" as const, parameter: "enabled" as const, label: "Dry lane · enabled", value: present.wdw.dry.enabled ? "On" : "Off" },
        { lane: "dry" as const, parameter: "levelDb" as const, label: "Dry lane · level", value: `${present.wdw.dry.levelDb.toFixed(1)} dB` },
        { lane: "dry" as const, parameter: "pan" as const, label: "Dry lane · pan", value: `${Math.round((present.wdw.dry.pan ?? 0) * 100)}%` },
        { lane: "wet" as const, parameter: "enabled" as const, label: "Wet lane · enabled", value: present.wdw.wet.enabled ? "On" : "Off" },
        { lane: "wet" as const, parameter: "levelDb" as const, label: "Wet lane · level", value: `${present.wdw.wet.levelDb.toFixed(1)} dB` },
        { lane: "wet" as const, parameter: "width" as const, label: "Wet lane · width", value: `${Math.round((present.wdw.wet.width ?? 1) * 100)}%` },
      ];
      for (const row of laneRows) {
        const key = `wdw:${row.lane}:${row.parameter}`;
        if (!sceneOwnedAddresses.has(key)) rows.push({ key, label: row.label, value: row.value });
      }
    }
    return rows;
  }, [allBlocks, present.global, present.routing, present.wdw, sceneOwnedAddresses]);
  const expressionTarget = expressionTargets.find(({ block }) =>
    block.id === present.expression?.blockId,
  );
  const expressionParameter = expressionTarget?.parameters.find(({ key }) =>
    key === present.expression?.parameter,
  );
  const patchExpression = (
    patch: Partial<NonNullable<typeof present.expression>>,
  ) => {
    if (!present.expression) return;
    dispatch({ type: "set-expression", expression: { ...present.expression, ...patch } });
  };
  const enableExpression = () => {
    const target = expressionTargets[0];
    const parameter = target?.parameters[0];
    if (!target || !parameter) return;
    dispatch({
      type: "set-expression",
      expression: {
        blockId: target.block.id,
        parameter: parameter.key,
        minimum: parameter.minimum,
        maximum: parameter.maximum,
        inverted: false,
      },
    });
  };
  const disabledDefinitions = useMemo(() => {
    const result = new Map<string, string>();
    if (addTarget?.kind === "wdw") {
      const lane = present.wdw?.[addTarget.lane];
      for (const definition of allEffectDefinitions()) {
        if (!isWdwBlockAllowed(addTarget.lane, definition.blockType)) {
          result.set(definition.id, `Not admitted on the ${wdwLaneLabel(addTarget.lane)} WDW lane`);
        }
      }
      if (lane) {
        for (const definition of allEffectDefinitions()) {
          if (!definition.constraintGroup || definition.maxEnabledInGroup !== 1) continue;
          const conflict = lane.blocks.find((block) => block.enabled
            && findEffectDefinition(block)?.constraintGroup === definition.constraintGroup);
          if (conflict) result.set(definition.id, `Disable ${findEffectDefinition(conflict)?.name ?? conflict.type} first`);
        }
      }
      return result;
    }
    if (addTarget?.kind === "lane") {
      result.set("dualAmp", "Split blocks cannot be nested");
      result.set("dualRig", "Split blocks cannot be nested");
      const rig = findPresetBlockInPreset(present, addTarget.rigId);
      const blocks = rig?.lanes?.[addTarget.lane].blocks ?? [];
      for (const definition of allEffectDefinitions()) {
        if (!definition.constraintGroup || definition.maxEnabledInGroup !== 1) continue;
        const conflict = blocks.find((block) => block.enabled
          && findEffectDefinition(block)?.constraintGroup === definition.constraintGroup);
        if (conflict) result.set(definition.id, `Disable ${findEffectDefinition(conflict)?.name ?? conflict.type} first`);
      }
      return result;
    }
    const enabledParallelRig = allBlocks.find((block) => block.enabled
      && (block.type === "dualAmp" || block.type === "dualRig"));
    const enabledStandaloneAmp = allBlocks.find((block) => block.enabled && (block.type === "nam" || block.type === "cab"));
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
      const conflict = allBlocks.find((block) => block.enabled && findEffectDefinition(block)?.constraintGroup === definition.constraintGroup);
      if (conflict) result.set(definition.id, `Disable ${findEffectDefinition(conflict)?.name ?? conflict.type} first`);
    }
    return result;
  }, [addTarget, allBlocks, present]);

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

  const apply = async (savedFirst = false): Promise<boolean> => {
    if (!validation.canApply) return false;
    setActionError(undefined);
    try {
      const response = await session.applyCurrent(editor.editingSceneId);
      if (response?.accepted && (!response.id || response.state === "applied")) {
        setApplied(editor.location);
        return true;
      }
      setActionError(savedFirst ? "Saved; pedal still playing the previous version." : "The pedal did not apply this preset.");
      return false;
    } catch (reason) {
      const detail = reason instanceof Error ? reason.message : "Could not apply preset.";
      setActionError(savedFirst ? `Saved; pedal still playing the previous version. ${detail}` : detail);
      return false;
    }
  };

  const saveAndApply = async () => { if (await save()) await apply(true); };

  const recallScene = async () => {
    if (!editor.editingSceneId) return;
    setActionError(undefined);
    setRecallingScene(true);
    try {
      if (!session.recallScene || !(await session.recallScene(editor.editingSceneId))) {
        setActionError("The pedal did not accept the scene recall.");
      }
    } catch (reason) {
      setActionError(reason instanceof Error ? reason.message : "Could not recall scene.");
    } finally {
      setRecallingScene(false);
    }
  };

  const resolveNavigation = async (choice: "save" | "discard" | "cancel") => {
    const destination = pendingLocation;
    setPendingLocation(undefined);
    if (!destination || choice === "cancel") return;
    if (choice === "save" && !(await save())) return;
    if (choice === "discard") dispatch({ type: "load", location: editor.location, preset: editor.saved });
    await session.selectLocation(destination);
  };

  if (session.status !== "connected" || !session.current) {
    return <main className="workspace workspace--offline"><div className="offline-card"><CloudOff size={38} /><p className="eyebrow">Ardor Manager</p><h1>Connect to your pedal</h1><p>Manage preset chains, models, cabinet IRs and reverb IRs from one desktop workspace.</p><Button variant="primary" onClick={onConnection}>Connect to device</Button></div></main>;
  }

  const locationLabel = `Bank ${String(editor.location.bank).padStart(3, "0")} / Slot ${editor.location.slot + 1}`;
  const applyBlocked = !validation.canApply || dirty || session.busy.apply || saving;
  const runtimeMatchesDraft = activeRevisionMatchesDraft(session.device, editor.location, dirty);
  const recallHint = dirty || !runtimeMatchesDraft
    ? "Apply this version to recall it."
    : "Scene recall is unavailable on this connection.";
  return (
    <main className="workspace">
      <PresetSidebar summaries={session.presets} selected={editor.location} active={session.device?.active} disabled={saving || session.busy.apply} onSelect={selectLocation} />
      <section className="workspace-main">
        <header className="preset-header">
          <div><p className="eyebrow">{locationLabel}</p><input aria-label="Preset name" className="preset-name-input" value={present.name} onChange={(event) => dispatch({ type: "set-name", name: event.target.value })} /><div className="preset-header__meta">{dirty && <StatusBadge tone="warning">Unsaved changes</StatusBadge>}{applied?.bank === editor.location.bank && applied.slot === editor.location.slot && <StatusBadge tone="success"><Check size={13} /> Applied this session</StatusBadge>}</div></div>
          <div className="preset-actions"><IconButton label="Undo" disabled={editor.history.past.length === 0} onClick={() => dispatch({ type: "undo" })}><Undo2 size={17} /></IconButton><IconButton label="Redo" disabled={editor.history.future.length === 0} onClick={() => dispatch({ type: "redo" })}><Redo2 size={17} /></IconButton>{!present.sceneSet && <Button variant="secondary" disabled={saving || (session.device?.supportedPresetVersion !== undefined && session.device.supportedPresetVersion < 4)} onClick={() => dispatch({ type: "enable-scenes" })}>Enable scenes</Button>}<Button variant="secondary" disabled={!dirty || !validation.canSave || saving} onClick={() => void save()}><Save size={16} /> {saving ? "Saving…" : "Save"}</Button><Button variant="primary" disabled={!validation.canApply || saving || session.busy.apply} onClick={() => void saveAndApply()}><Send size={16} /> Save & Apply</Button><Button variant="quiet" disabled={applyBlocked} onClick={() => void apply()}>Apply</Button></div>
        </header>
        {present.sceneSet && <SceneWorkspaceBar
          sceneSet={present.sceneSet}
          editingSceneId={editor.editingSceneId ?? present.sceneSet.defaultSceneId}
          liveSceneId={session.device?.active?.liveSceneId}
          recallDisabled={!runtimeMatchesDraft || !session.device?.capabilities.sceneRecall}
          recallHint={recallHint}
          recalling={recallingScene}
          onSelect={(sceneId) => dispatch({ type: "select-scene", sceneId })}
          onRecall={() => void recallScene()}
          onName={(sceneId, name) => dispatch({ type: "set-scene-name", sceneId, name })}
          onEnterTime={(sceneId, value) => dispatch({ type: "set-scene-enter-time", sceneId, value })}
          onTrim={(sceneId, value) => dispatch({ type: "set-scene-trim", sceneId, value })}
          onDefault={(sceneId) => dispatch({ type: "set-default-scene", sceneId })}
          onOpenIn={(value) => dispatch({ type: "set-scene-open-in", value })}
          onCopy={(sourceSceneId, destinationSceneId) => dispatch({ type: "copy-scene", sourceSceneId, destinationSceneId })}
          onSwap={(firstSceneId, secondSceneId) => dispatch({ type: "swap-scenes", firstSceneId, secondSceneId })}
          presentTarget={presentSceneTarget}
          sharedRows={sharedComparisonRows}
          onCopyRow={(rowKey, sourceSceneId) => dispatch({ type: "copy-scene-row-across", rowKey, sourceSceneId })}
        />}
        <div className="global-strip"><SlidersHorizontal size={17} /><label>Input<input aria-label="Input gain" type="number" min={-60} max={24} value={displayedInputGain} onChange={(event) => {
          const value = Number(event.target.value);
          if (sceneInputOwned && editingScene) dispatch({ type: "set-scene-input-gain", sceneId: editingScene.id, value });
          else dispatch({ type: "set-global", key: "inputGainDb", value });
        }} /><small>dB</small></label>{editingScene && <label>Input scope<select aria-label="Input gain scope" value={sceneInputOwned ? "scene" : "shared"} onChange={(event) => dispatch({ type: "set-scene-input-scope", sceneId: editingScene.id, scope: event.target.value as "shared" | "scene" })}><option value="shared">Shared</option><option value="scene">This scene</option></select></label>}<label>Output<input type="number" min={-60} max={24} value={present.global.outputGainDb} onChange={(event) => dispatch({ type: "set-global", key: "outputGainDb", value: Number(event.target.value) })} /><small>dB</small></label><label className="routing-picker">Topology<select aria-label="Preset topology" value={present.routing} onChange={(event) => dispatch({ type: "set-routing", routing: event.target.value as "serial" | "wdw" })}><option value="serial">Serial</option><option value="wdw">Wet / dry / wet</option></select></label><span>{present.routing === "wdw" ? "Two complete lanes · bounded pair execution" : "Serial routing"}</span></div>
        <div className="expression-strip">
          <label className="expression-strip__enable">
            <input
              type="checkbox"
              checked={present.expression !== undefined}
              disabled={expressionTargets.length === 0}
              onChange={(event) => {
                if (event.target.checked) enableExpression();
                else dispatch({ type: "set-expression" });
              }}
            />
            Expression pedal
          </label>
          {present.expression ? <>
            <label>Effect
              <select
                aria-label="Expression effect"
                value={present.expression.blockId}
                onChange={(event) => {
                  const target = expressionTargets.find(({ block }) => block.id === event.target.value);
                  const parameter = target?.parameters[0];
                  if (!target || !parameter) return;
                  dispatch({
                    type: "set-expression",
                    expression: {
                      blockId: target.block.id,
                      parameter: parameter.key,
                      minimum: parameter.minimum,
                      maximum: parameter.maximum,
                      inverted: present.expression?.inverted ?? false,
                    },
                  });
                }}
              >
                {expressionTargets.map(({ block, name }) =>
                  <option key={block.id} value={block.id}>{name} · {block.id}</option>)}
              </select>
            </label>
            <label>Parameter
              <select
                aria-label="Expression parameter"
                value={present.expression.parameter}
                onChange={(event) => {
                  const parameter = expressionTarget?.parameters.find(({ key }) => key === event.target.value);
                  if (!parameter) return;
                  patchExpression({
                    parameter: parameter.key,
                    minimum: parameter.minimum,
                    maximum: parameter.maximum,
                  });
                }}
              >
                {expressionTarget?.parameters.map((parameter) =>
                  <option key={parameter.key} value={parameter.key}>{parameter.label}</option>)}
              </select>
            </label>
            <label>Minimum
              <input
                aria-label="Expression minimum"
                type="number"
                step={expressionParameter?.step ?? "any"}
                value={present.expression.minimum}
                onChange={(event) => patchExpression({ minimum: Number(event.target.value) })}
              />
            </label>
            <label>Maximum
              <input
                aria-label="Expression maximum"
                type="number"
                step={expressionParameter?.step ?? "any"}
                value={present.expression.maximum}
                onChange={(event) => patchExpression({ maximum: Number(event.target.value) })}
              />
            </label>
            <label>
              <input
                type="checkbox"
                checked={present.expression.inverted}
                onChange={(event) => patchExpression({ inverted: event.target.checked })}
              />
              Invert
            </label>
          </> : <small>
            {expressionTargets.length > 0
              ? "Enable to assign one effect parameter in this preset."
              : "Add a supported effect before assigning the pedal."}
          </small>}
        </div>
        {(!validation.canSave || validation.issues.length > 0 || actionError) && <div className="workspace-alert" role="alert"><AlertCircle size={17} /><div>{actionError ? <p>{actionError}</p> : <p>{validation.issues[0]?.message}</p>}<small>{!validation.canSave ? "Fix this before saving." : !validation.canApply ? "You can save this draft, but cannot apply it yet." : "Review the highlighted block."}</small></div></div>}
        {present.routing === "wdw" && displayedWdw
          ? <WdwRoutingCanvas routing={displayedWdw} selectedBlockId={editor.selectedBlockId} issuesFor={(id) => issuesForBlock(validation, id)} maxed={allBlocks.length >= 20} onSelect={(blockId) => dispatch({ type: "select-block", blockId })} onAdd={(lane, index) => setAddTarget({ kind: "wdw", lane, index })} onMove={(lane, blockId, index) => dispatch({ type: "move-wdw-block", lane, blockId, index })} onToggle={editBlockEnabled} onDuplicate={(blockId) => dispatch({ type: "duplicate-block", blockId })} onReset={(blockId) => dispatch({ type: "reset-block", blockId })} onDelete={(blockId) => dispatch({ type: "remove-block", blockId })} onMix={editWdwMix} />
          : <ChainCanvas blocks={displayedBlocks} selectedBlockId={editor.selectedBlockId} issuesFor={(id) => issuesForBlock(validation, id)} maxed={present.blocks.length >= 10} onSelect={(blockId) => dispatch({ type: "select-block", blockId })} onAdd={(index) => setAddTarget({ kind: "top", index })} onMove={(blockId, index) => dispatch({ type: "move-block", blockId, index })} onLaneAdd={(rigId, lane, index) => setAddTarget({ kind: "lane", rigId, lane, index })} onLaneMove={(rigId, blockId, lane, index) => dispatch({ type: "move-lane-block", rigId, blockId, lane, index })} onToggle={editBlockEnabled} onDuplicate={(blockId) => dispatch({ type: "duplicate-block", blockId })} onReset={(blockId) => dispatch({ type: "reset-block", blockId })} onDelete={(blockId) => dispatch({ type: "remove-block", blockId })} />}
      </section>
      <BlockInspector block={inspectorBlock} issues={selected ? issuesForBlock(validation, selected.id) : []} models={session.models} irs={session.irs} reverbIrs={session.reverbIrs} scenesEnabled={present.version === 4} onToggle={editBlockEnabled} onParam={editParameter} sceneScopeFor={sceneScopeFor} onSceneScope={(blockId, parameter, scope, value) => editingScene && dispatch({ type: "set-scene-scope", sceneId: editingScene.id, blockId, parameter, scope, value })} onSceneBypass={(blockId, policy) => dispatch({ type: "set-scene-bypass", blockId, policy })} onAsset={(blockId, asset) => dispatch({ type: "set-block-asset", blockId, asset })} onMode={(blockId, definitionId) => dispatch({ type: "change-definition", blockId, definitionId })} onEqBand={(blockId, band, patch) => dispatch({ type: "set-eq-band", blockId, band, patch })} onReset={(blockId) => dispatch({ type: "reset-block", blockId })} onDuplicate={(blockId) => dispatch({ type: "duplicate-block", blockId })} onDelete={(blockId) => dispatch({ type: "remove-block", blockId })} onAssets={onAssets} onClose={() => dispatch({ type: "select-block" })} />
      <BlockBrowser open={addTarget !== undefined} onOpenChange={(open) => { if (!open) setAddTarget(undefined); }} disabledIds={disabledDefinitions} onChoose={(definition) => {
        if (addTarget?.kind === "lane") {
          dispatch({ type: "add-lane-block", definitionId: definition.id, rigId: addTarget.rigId, lane: addTarget.lane, index: addTarget.index });
        } else if (addTarget?.kind === "wdw") {
          dispatch({ type: "add-wdw-block", definitionId: definition.id, lane: addTarget.lane, index: addTarget.index });
        } else {
          dispatch({ type: "add-block", definitionId: definition.id, index: addTarget?.index ?? present.blocks.length });
        }
        setAddTarget(undefined);
      }} />
      <UnsavedChangesDialog open={pendingLocation !== undefined} busy={saving} onChoice={(choice) => void resolveNavigation(choice)} />
    </main>
  );
}
