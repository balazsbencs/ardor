import { useEffect, useMemo, useReducer, useState, type Dispatch } from "react";

import type { Preset, PresetBlock, PresetScene, PresetSceneTarget, WdwRouting } from "../../api/types";
import { displayValue } from "../../components/ParameterSlider";
import { useDeviceSession } from "../../connection/deviceSession";
import { allEffectDefinitions, findEffectDefinition } from "../../effects/catalog";
import type { NumberControl } from "../../effects/types";
import { activeRevisionMatchesDraft } from "../scenes/sceneRevision";
import type { SharedComparisonRow } from "../scenes/SceneWorkspaceBar";
import { applySceneToBlock, applySceneToBlocks, sceneOwns } from "../scenes/sceneView";
import { allPresetBlocksInPreset, createEditorState, editorReducer, findPresetBlockInPreset, isEditorDirty } from "./editorReducer";
import type { EditorAction, EditorState, PresetLocation } from "./editorTypes";
import { validatePreset, type PresetValidationResult } from "./presetValidation";
import { isWdwBlockAllowed, wdwLaneLabel } from "./wdwPolicy";

export type AddTarget =
  | { kind: "top"; index: number }
  | { kind: "lane"; rigId: string; lane: "left" | "right"; index: number }
  | { kind: "wdw"; lane: "dry" | "wet"; index: number };

export type ExpressionTarget = { block: PresetBlock; name: string; parameters: NumberControl[] };

export type PresetEditor = {
  editor: EditorState;
  dispatch: Dispatch<EditorAction>;
  present: Preset;
  dirty: boolean;
  validation: PresetValidationResult;
  allBlocks: PresetBlock[];
  editingScene?: PresetScene;
  displayedBlocks: PresetBlock[];
  displayedWdw?: WdwRouting;
  inspectorBlock?: PresetBlock;
  displayedInputGain: number;
  sceneInputOwned: boolean;
  sceneScopeFor(blockId: string, parameter?: string): "shared" | "scene";
  editBlockEnabled(blockId: string, enabled: boolean): void;
  editParameter(blockId: string, parameter: string, value: unknown, gesture?: string): void;
  editWdwMix(lane: "dry" | "wet", key: "levelDb" | "pan" | "width" | "enabled", value: number | boolean): void;
  expressionTargets: ExpressionTarget[];
  expressionTarget?: ExpressionTarget;
  expressionParameter?: NumberControl;
  enableExpression(): void;
  patchExpression(patch: Partial<NonNullable<Preset["expression"]>>): void;
  addTarget?: AddTarget;
  setAddTarget(target?: AddTarget): void;
  disabledDefinitions: Map<string, string>;
  selectLocation(location: PresetLocation): void;
  pendingLocation?: PresetLocation;
  resolveNavigation(choice: "save" | "discard" | "cancel"): Promise<void>;
  save(): Promise<boolean>;
  apply(savedFirst?: boolean): Promise<boolean>;
  saveAndApply(): Promise<void>;
  recallScene(): Promise<void>;
  saving: boolean;
  recallingScene: boolean;
  actionError?: string;
  setActionError(message?: string): void;
  applied?: PresetLocation;
  runtimeMatchesDraft: boolean;
  sharedComparisonRows: SharedComparisonRow[];
  presentSceneTarget(target: PresetSceneTarget, value: number | boolean): { label: string; value: string };
};

export function usePresetEditor(): PresetEditor {
  const session = useDeviceSession();
  const [editor, dispatch] = useReducer(editorReducer, undefined, () => session.current
    ? createEditorState(session.current.location, session.current.preset)
    : createEditorState({ bank: 0, slot: 0 }, {
      version: 1, name: "New Preset", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [],
    }));
  const [addTarget, setAddTarget] = useState<AddTarget>();
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
  const displayedBlocks = useMemo(() => applySceneToBlocks(present.blocks, editingScene), [present.blocks, editingScene]);
  const displayedWdw = useMemo<WdwRouting | undefined>(() => {
    if (!present.wdw) return undefined;
    const routing = structuredClone(present.wdw);
    routing.dry.blocks = applySceneToBlocks(routing.dry.blocks, editingScene);
    routing.wet.blocks = applySceneToBlocks(routing.wet.blocks, editingScene);
    for (const target of editingScene?.targets ?? []) {
      if (target.target !== "wdwLane") continue;
      const lane = routing[target.lane];
      if (target.parameter === "enabled" && typeof target.value === "boolean") lane.enabled = target.value;
      else if (target.parameter === "levelDb" && typeof target.value === "number") lane.levelDb = target.value;
      else if (target.parameter === "pan" && typeof target.value === "number") lane.pan = target.value;
      else if (target.parameter === "width" && typeof target.value === "number") lane.width = target.value;
    }
    return routing;
  }, [present.wdw, editingScene]);
  const inspectorBlock = useMemo(() => (selected ? applySceneToBlock(selected, editingScene) : undefined), [selected, editingScene]);
  const sceneScopeFor = (blockId: string, parameter?: string): "shared" | "scene" =>
    sceneOwns(editingScene, blockId, parameter) ? "scene" : "shared";
  const editBlockEnabled = (blockId: string, enabled: boolean) => {
    if (editingScene && sceneScopeFor(blockId) === "scene") {
      dispatch({ type: "set-scene-block-enabled", sceneId: editingScene.id, blockId, value: enabled });
    } else dispatch({ type: "toggle-block", blockId, enabled });
  };
  const editParameter = (blockId: string, parameter: string, value: unknown, gesture?: string) => {
    if (editingScene && typeof value === "number" && sceneScopeFor(blockId, parameter) === "scene") {
      dispatch({ type: "set-scene-parameter", sceneId: editingScene.id, blockId, parameter, value, gesture });
    } else dispatch({ type: "set-block-param", blockId, key: parameter, value, gesture });
  };
  const editWdwMix = (lane: "dry" | "wet", key: "levelDb" | "pan" | "width" | "enabled", value: number | boolean) => {
    if (editingScene?.targets.some((target) => target.target === "wdwLane"
        && target.lane === lane && target.parameter === key)) {
      dispatch({ type: "set-scene-wdw-mix", sceneId: editingScene.id, lane, key, value });
    } else dispatch({ type: "set-wdw-mix", lane, key, value });
  };
  const expressionTargets = useMemo<ExpressionTarget[]>(() => allBlocks.flatMap((block) => {
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

  const runtimeMatchesDraft = activeRevisionMatchesDraft(session.device, editor.location, dirty);

  return {
    editor, dispatch, present, dirty, validation, allBlocks, editingScene, displayedBlocks, displayedWdw,
    inspectorBlock, displayedInputGain, sceneInputOwned, sceneScopeFor, editBlockEnabled, editParameter,
    editWdwMix, expressionTargets, expressionTarget, expressionParameter, enableExpression, patchExpression,
    addTarget, setAddTarget, disabledDefinitions, selectLocation, pendingLocation, resolveNavigation, save,
    apply, saveAndApply, recallScene, saving, recallingScene, actionError, setActionError, applied,
    runtimeMatchesDraft, sharedComparisonRows, presentSceneTarget,
  };
}
