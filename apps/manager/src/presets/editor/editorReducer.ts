import type { Preset, PresetBlock, PresetSceneTarget } from "../../api/types";
import {
  createBlockFromDefinition,
  defaultsForDefinition,
  findEffectDefinition,
  getEffectDefinition,
} from "../../effects/catalog";
import type { NumberControl } from "../../effects/types";
import type { EditorAction, EditorState, EqBand, PresetLocation } from "./editorTypes";
import { clonePreset, createEmptyWdwRouting, nextPresetBlockId } from "./presetFactory";
import { isWdwBlockAllowed } from "./wdwPolicy";

const historyLimit = 100;

export function deepEqual(left: unknown, right: unknown): boolean {
  if (Object.is(left, right)) return true;
  if (typeof left !== typeof right || left === null || right === null) return false;
  if (Array.isArray(left) || Array.isArray(right)) {
    if (!Array.isArray(left) || !Array.isArray(right) || left.length !== right.length) return false;
    return left.every((value, index) => deepEqual(value, right[index]));
  }
  if (typeof left !== "object" || typeof right !== "object") return false;
  const leftRecord = left as Record<string, unknown>;
  const rightRecord = right as Record<string, unknown>;
  const leftKeys = Object.keys(leftRecord);
  const rightKeys = Object.keys(rightRecord);
  return leftKeys.length === rightKeys.length
    && leftKeys.every((key) => Object.prototype.hasOwnProperty.call(rightRecord, key)
      && deepEqual(leftRecord[key], rightRecord[key]));
}

export function createEditorState(location: PresetLocation, preset: Preset): EditorState {
  const saved = clonePreset(preset);
  return {
    location: { ...location },
    saved,
    history: { past: [], present: clonePreset(preset), future: [] },
    editingSceneId: preset.sceneSet?.defaultSceneId,
  };
}

export function isEditorDirty(state: EditorState): boolean {
  return !deepEqual(state.saved, state.history.present);
}

function withMutation(
  state: EditorState,
  update: (present: Preset) => Preset | undefined,
  selectedBlockId: string | undefined = state.selectedBlockId,
): EditorState {
  const next = update(state.history.present);
  if (!next || deepEqual(next, state.history.present)) return state;
  const past = [...state.history.past, clonePreset(state.history.present)].slice(-historyLimit);
  return {
    ...state,
    selectedBlockId,
    history: { past, present: next, future: [] },
  };
}

function finiteValue(value: unknown): boolean {
  if (typeof value === "number") return Number.isFinite(value);
  if (Array.isArray(value)) return value.every(finiteValue);
  if (typeof value === "object" && value !== null) return Object.values(value).every(finiteValue);
  return true;
}

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.min(maximum, Math.max(minimum, value));
}

function sceneTargetKey(target: PresetSceneTarget): string {
  if (target.target === "inputGainDb") return "inputGainDb";
  if (target.target === "parameter") return `parameter:${target.blockId}:${target.parameter}`;
  if (target.target === "blockEnabled") return `enabled:${target.blockId}`;
  return `wdw:${target.lane}:${target.parameter}`;
}

function wdwLaneForBlock(preset: Preset, blockId: string): "dry" | "wet" | undefined {
  if (preset.routing !== "wdw" || !preset.wdw) return undefined;
  if (preset.wdw.dry.blocks.some(({ id }) => id === blockId)) return "dry";
  if (preset.wdw.wet.blocks.some(({ id }) => id === blockId)) return "wet";
  return undefined;
}

export function allPresetBlocks(blocks: PresetBlock[]): PresetBlock[] {
  return blocks.flatMap((block) => [
    block,
    ...allPresetBlocks(block.lanes?.left.blocks ?? []),
    ...allPresetBlocks(block.lanes?.right.blocks ?? []),
  ]);
}

export function allPresetBlocksInPreset(preset: Preset): PresetBlock[] {
  if (preset.routing !== "wdw" || !preset.wdw) return allPresetBlocks(preset.blocks);
  return [
    ...allPresetBlocks(preset.wdw.dry.blocks),
    ...allPresetBlocks(preset.wdw.wet.blocks),
  ];
}

export function findPresetBlock(blocks: PresetBlock[], blockId: string | undefined): PresetBlock | undefined {
  if (!blockId) return undefined;
  return allPresetBlocks(blocks).find(({ id }) => id === blockId);
}

export function findPresetBlockInPreset(preset: Preset, blockId: string | undefined): PresetBlock | undefined {
  if (!blockId) return undefined;
  return allPresetBlocksInPreset(preset).find(({ id }) => id === blockId);
}

function updateBlockTree(
  blocks: PresetBlock[],
  blockId: string,
  update: (block: PresetBlock) => PresetBlock,
): boolean {
  for (let index = 0; index < blocks.length; index += 1) {
    if (blocks[index].id === blockId) {
      blocks[index] = update(blocks[index]);
      return true;
    }
    const lanes = blocks[index].lanes;
    if (lanes && (updateBlockTree(lanes.left.blocks, blockId, update)
      || updateBlockTree(lanes.right.blocks, blockId, update))) {
      return true;
    }
  }
  return false;
}

function updatedBlock(preset: Preset, blockId: string, update: (block: PresetBlock) => PresetBlock): Preset | undefined {
  const next = clonePreset(preset);
  if (updateBlockTree(next.blocks, blockId, update)) return next;
  if (next.wdw && (updateBlockTree(next.wdw.dry.blocks, blockId, update)
    || updateBlockTree(next.wdw.wet.blocks, blockId, update))) return next;
  return undefined;
}

function normalizedControlValue(block: PresetBlock, key: string, value: unknown): unknown | undefined {
  if (!finiteValue(value)) return undefined;
  const definition = findEffectDefinition(block);
  const control = definition?.controls.find((candidate) => "key" in candidate && candidate.key === key);
  if (!control) return value;
  if (control.kind === "number") {
    if (typeof value !== "number") return undefined;
    return clamp(value, control.minimum, control.maximum);
  }
  if (control.kind === "choice") {
    return typeof value === "string" && control.choices.some(({ value: choice }) => choice === value)
      ? value
      : undefined;
  }
  if (control.kind === "toggle") return typeof value === "boolean" ? value : undefined;
  return undefined;
}

function defaultEqBands(): EqBand[] {
  const defaults = defaultsForDefinition("eq:parametric_eq_5").bands;
  return structuredClone(defaults) as EqBand[];
}

function currentEqBands(block: PresetBlock): EqBand[] {
  const defaults = defaultEqBands();
  const supplied = Array.isArray(block.params.bands) ? block.params.bands : [];
  return defaults.map((fallback, index) => {
    const value = supplied[index];
    return typeof value === "object" && value !== null && !Array.isArray(value)
      ? { ...fallback, ...(value as Record<string, unknown>) } as EqBand
      : fallback;
  });
}

function setEqBand(state: EditorState, blockId: string, band: number, patch: Partial<EqBand>): EditorState {
  if (!Number.isInteger(band) || band < 0 || band >= 5 || !finiteValue(patch)) return state;
  if (patch.enabled !== undefined && typeof patch.enabled !== "boolean") return state;
  const ranges: Record<string, [number, number]> = {
    frequency_hz: [20, 20000], q: [0.1, 18], gain_db: [-18, 18],
  };
  const normalized: Record<string, unknown> = { ...patch };
  for (const [key, [minimum, maximum]] of Object.entries(ranges)) {
    if (normalized[key] === undefined) continue;
    if (typeof normalized[key] !== "number") return state;
    normalized[key] = clamp(normalized[key], minimum, maximum);
  }
  return withMutation(state, (present) => updatedBlock(present, blockId, (block) => {
    if (block.type !== "eq" || block.params.mode !== "parametric_eq_5") return block;
    const bands = currentEqBands(block);
    bands[band] = { ...bands[band], ...normalized } as EqBand;
    return { ...block, params: { ...block.params, bands } };
  }));
}

function setBlockParam(state: EditorState, blockId: string, key: string, value: unknown): EditorState {
  const block = findPresetBlockInPreset(state.history.present, blockId);
  if (!block) return state;
  const normalized = normalizedControlValue(block, key, value);
  if (normalized === undefined) return state;
  return withMutation(state, (present) => updatedBlock(present, blockId, (candidate) => ({
    ...candidate,
    params: { ...candidate.params, [key]: structuredClone(normalized) },
  })));
}

function resetKnownParams(block: PresetBlock): PresetBlock {
  const definition = findEffectDefinition(block);
  if (!definition) return block;
  return {
    ...block,
    params: { ...block.params, ...defaultsForDefinition(definition.id) },
  };
}

export function editorReducer(state: EditorState, action: EditorAction): EditorState {
  switch (action.type) {
    case "load":
      return createEditorState(action.location, action.preset);
    case "select-block":
      return state.selectedBlockId === action.blockId ? state : { ...state, selectedBlockId: action.blockId };
    case "select-scene":
      return state.history.present.sceneSet?.scenes.some(({ id }) => id === action.sceneId)
        ? { ...state, editingSceneId: action.sceneId }
        : state;
    case "set-scene-name":
      return withMutation(state, (present) => {
        if (!present.sceneSet) return undefined;
        const next = clonePreset(present);
        const scene = next.sceneSet?.scenes.find(({ id }) => id === action.sceneId);
        if (!scene) return undefined;
        scene.name = action.name.slice(0, 24);
        return next;
      });
    case "set-scene-enter-time":
      if (!Number.isFinite(action.value)) return state;
      return withMutation(state, (present) => {
        if (!present.sceneSet) return undefined;
        const next = clonePreset(present);
        const scene = next.sceneSet?.scenes.find(({ id }) => id === action.sceneId);
        if (!scene) return undefined;
        scene.enterTimeMs = clamp(Math.round(action.value), 0, 10000);
        return next;
      });
    case "set-scene-trim":
      if (!Number.isFinite(action.value)) return state;
      return withMutation(state, (present) => {
        if (!present.sceneSet) return undefined;
        const next = clonePreset(present);
        const scene = next.sceneSet?.scenes.find(({ id }) => id === action.sceneId);
        if (!scene) return undefined;
        scene.outputTrimDb = clamp(action.value, -12, 6);
        return next;
      });
    case "set-default-scene":
      return withMutation(state, (present) => {
        if (!present.sceneSet?.scenes.some(({ id }) => id === action.sceneId)) return undefined;
        const next = clonePreset(present);
        if (next.sceneSet) next.sceneSet.defaultSceneId = action.sceneId;
        return next;
      });
    case "set-scene-open-in":
      return withMutation(state, (present) => {
        if (!present.sceneSet) return undefined;
        const next = clonePreset(present);
        if (next.sceneSet) next.sceneSet.openIn = action.value;
        return next;
      });
    case "copy-scene":
      if (action.sourceSceneId === action.destinationSceneId) return state;
      return withMutation(state, (present) => {
        if (!present.sceneSet) return undefined;
        const next = clonePreset(present);
        const source = next.sceneSet?.scenes.find(({ id }) => id === action.sourceSceneId);
        const destination = next.sceneSet?.scenes.find(({ id }) => id === action.destinationSceneId);
        if (!source || !destination) return undefined;
        destination.enterTimeMs = source.enterTimeMs;
        destination.outputTrimDb = source.outputTrimDb;
        destination.targets = structuredClone(source.targets);
        return next;
      });
    case "swap-scenes":
      if (action.firstSceneId === action.secondSceneId) return state;
      return withMutation(state, (present) => {
        if (!present.sceneSet) return undefined;
        const next = clonePreset(present);
        const first = next.sceneSet!.scenes.findIndex(({ id }) => id === action.firstSceneId);
        const second = next.sceneSet!.scenes.findIndex(({ id }) => id === action.secondSceneId);
        if (first < 0 || second < 0) return undefined;
        [next.sceneSet!.scenes[first], next.sceneSet!.scenes[second]] = [
          next.sceneSet!.scenes[second], next.sceneSet!.scenes[first],
        ];
        return next;
      });
    case "copy-scene-row-across":
      return withMutation(state, (present) => {
        if (!present.sceneSet) return undefined;
        const next = clonePreset(present);
        const source = next.sceneSet!.scenes.find(({ id }) => id === action.sourceSceneId);
        if (!source) return undefined;
        if (action.rowKey === "enterTime") {
          for (const scene of next.sceneSet!.scenes) scene.enterTimeMs = source.enterTimeMs;
          return next;
        }
        if (action.rowKey === "trim") {
          for (const scene of next.sceneSet!.scenes) scene.outputTrimDb = source.outputTrimDb;
          return next;
        }
        const sourceTarget = source.targets.find((target) => sceneTargetKey(target) === action.rowKey);
        if (!sourceTarget) return undefined;
        for (const scene of next.sceneSet!.scenes) {
          const index = scene.targets.findIndex((target) => sceneTargetKey(target) === action.rowKey);
          if (index < 0) scene.targets.push(structuredClone(sourceTarget));
          else scene.targets[index] = structuredClone(sourceTarget);
        }
        return next;
      });
    case "set-scene-parameter":
      if (!Number.isFinite(action.value)) return state;
      return withMutation(state, (present) => {
        if (!present.sceneSet) return undefined;
        const next = clonePreset(present);
        const scene = next.sceneSet?.scenes.find(({ id }) => id === action.sceneId);
        const target = scene?.targets.find((candidate) => candidate.target === "parameter"
          && candidate.blockId === action.blockId && candidate.parameter === action.parameter);
        if (!target || target.target !== "parameter") return undefined;
        target.value = action.value;
        return next;
      });
    case "set-scene-block-enabled":
      return withMutation(state, (present) => {
        if (!present.sceneSet) return undefined;
        const next = clonePreset(present);
        const scene = next.sceneSet?.scenes.find(({ id }) => id === action.sceneId);
        const target = scene?.targets.find((candidate) => candidate.target === "blockEnabled"
          && candidate.blockId === action.blockId);
        if (!target || target.target !== "blockEnabled") return undefined;
        target.value = action.value;
        return next;
      });
    case "set-scene-scope":
      return withMutation(state, (present) => {
        if (!present.sceneSet) return undefined;
        const next = clonePreset(present);
        const matches = (target: PresetSceneTarget) =>
          action.parameter !== undefined
            ? target.target === "parameter" && target.blockId === action.blockId && target.parameter === action.parameter
            : target.target === "blockEnabled" && target.blockId === action.blockId;
        if (action.scope === "scene") {
          for (const scene of next.sceneSet!.scenes) {
            if (scene.targets.some(matches)) continue;
            if (action.parameter !== undefined && typeof action.value === "number") {
              scene.targets.push({ target: "parameter", blockId: action.blockId, parameter: action.parameter, value: action.value });
            } else if (action.parameter === undefined && typeof action.value === "boolean") {
              scene.targets.push({ target: "blockEnabled", blockId: action.blockId, value: action.value });
            }
          }
          return next;
        }
        const editingScene = next.sceneSet!.scenes.find(({ id }) => id === action.sceneId);
        const owned = editingScene?.targets.find(matches);
        if (!owned) return undefined;
        const applyShared = (blocks: PresetBlock[]) => updateBlockTree(blocks, action.blockId, (block) => {
          if (owned.target === "parameter") return { ...block, params: { ...block.params, [owned.parameter]: owned.value } };
          if (owned.target === "blockEnabled") return { ...block, enabled: owned.value };
          return block;
        });
        applyShared(next.blocks) || (next.wdw && (applyShared(next.wdw.dry.blocks) || applyShared(next.wdw.wet.blocks)));
        for (const scene of next.sceneSet!.scenes) scene.targets = scene.targets.filter((target) => !matches(target));
        return next;
      });
    case "set-name":
      return withMutation(state, (present) => ({ ...clonePreset(present), name: action.name }));
    case "set-global": {
      if (!Number.isFinite(action.value)) return state;
      return withMutation(state, (present) => {
        const next = clonePreset(present);
        next.global[action.key] = clamp(action.value, -60, 24);
        return next;
      });
    }
    case "set-routing": {
      if (action.routing === state.history.present.routing) return state;
      if (action.routing === "wdw") {
        return withMutation(state, (present) => {
          const next = clonePreset(present);
          next.version = next.sceneSet ? 4 : 3;
          next.routing = "wdw";
          next.wdw = createEmptyWdwRouting();
          // Keep an existing serial draft audible and editable by placing it
          // in the dry lane. The wet starter pair receives fresh IDs so a
          // topology switch never silently drops the user's blocks or creates
          // duplicate MIDI/expression targets.
          next.wdw.dry.blocks = structuredClone(present.blocks);
          const occupied = allPresetBlocks(next.wdw.dry.blocks);
          const freshWet: PresetBlock[] = [];
          for (const block of next.wdw.wet.blocks) {
            const copy = structuredClone(block);
            copy.id = nextPresetBlockId([...occupied, ...freshWet]);
            freshWet.push(copy);
          }
          next.wdw.wet.blocks = freshWet;
          next.blocks = [];
          return next;
        });
      }
      return withMutation(state, (present) => ({
        ...clonePreset(present),
        version: present.sceneSet ? 4 : 2,
        routing: "serial",
        blocks: present.wdw?.dry.blocks.length || present.wdw?.wet.blocks.length
          ? [...(present.wdw?.dry.blocks ?? []), ...(present.wdw?.wet.blocks ?? [])]
          : present.blocks,
        wdw: undefined,
      }));
    }
    case "set-wdw-mix": {
      const current = state.history.present.wdw;
      if (state.history.present.routing !== "wdw" || !current) return state;
      if ((action.key === "pan" && action.lane !== "dry")
          || (action.key === "width" && action.lane !== "wet")) return state;
      if (typeof action.value === "number" && !Number.isFinite(action.value)) return state;
      return withMutation(state, (present) => {
        if (!present.wdw) return present;
        const next = clonePreset(present);
        const lane = next.wdw?.[action.lane];
        if (!lane) return present;
        if (action.key === "enabled") {
          if (typeof action.value !== "boolean") return present;
          lane.enabled = action.value;
        } else if (typeof action.value === "number") {
          const ranges = { levelDb: [-60, 12], pan: [-1, 1], width: [0, 1] } as const;
          const [minimum, maximum] = ranges[action.key];
          lane[action.key] = clamp(action.value, minimum, maximum);
        }
        return next;
      });
    }
    case "set-expression":
      if (action.expression
          && (!Number.isFinite(action.expression.minimum)
            || !Number.isFinite(action.expression.maximum))) return state;
      return withMutation(state, (present) => {
        const next = clonePreset(present);
        if (action.expression) next.expression = structuredClone(action.expression);
        else delete next.expression;
        return next;
      });
    case "add-block": {
      if (state.history.present.blocks.length >= 10) return state;
      let block: PresetBlock;
      try {
        block = createBlockFromDefinition(action.definitionId, state.history.present.blocks, action.initialAsset);
      } catch {
        return state;
      }
      return withMutation(state, (present) => {
        const next = clonePreset(present);
        const index = clamp(Math.trunc(action.index), 0, next.blocks.length);
        next.blocks.splice(index, 0, block);
        if (block.type === "dualRig") next.version = next.sceneSet ? 4 : 2;
        return next;
      }, block.id);
    }
    case "move-block": {
      const sourceIndex = state.history.present.blocks.findIndex(({ id }) => id === action.blockId);
      if (sourceIndex < 0 || !Number.isFinite(action.index)) return state;
      return withMutation(state, (present) => {
        const next = clonePreset(present);
        const [block] = next.blocks.splice(sourceIndex, 1);
        const destination = clamp(Math.trunc(action.index), 0, next.blocks.length);
        next.blocks.splice(destination, 0, block);
        return next;
      }, action.blockId);
    }
    case "add-lane-block": {
      if (action.definitionId === "dualRig" || action.definitionId === "dualAmp") return state;
      const rig = findPresetBlock(state.history.present.blocks, action.rigId);
      const lane = rig?.lanes?.[action.lane];
      if (!rig || rig.type !== "dualRig" || !lane || lane.blocks.length >= 10) return state;
      let block: PresetBlock;
      try {
        block = createBlockFromDefinition(
          action.definitionId,
          allPresetBlocks(state.history.present.blocks),
          action.initialAsset,
        );
      } catch {
        return state;
      }
      return withMutation(state, (present) => updatedBlock(present, action.rigId, (candidate) => {
        const next = structuredClone(candidate);
        const blocks = next.lanes?.[action.lane].blocks;
        if (!blocks) return candidate;
        blocks.splice(clamp(Math.trunc(action.index), 0, blocks.length), 0, block);
        return next;
      }), block.id);
    }
    case "add-wdw-block": {
      const routing = state.history.present.wdw;
      if (state.history.present.routing !== "wdw" || !routing) return state;
      if (!isWdwBlockAllowed(action.lane, action.definitionId.split(":", 1)[0])) return state;
      const lane = routing[action.lane];
      if (lane.blocks.length >= 10) return state;
      let block: PresetBlock;
      try {
        block = createBlockFromDefinition(
          action.definitionId,
          allPresetBlocksInPreset(state.history.present),
          action.initialAsset,
        );
      } catch {
        return state;
      }
      if ((block.type === "nam" || block.type === "cab")
          && lane.blocks.some(({ type }) => type === block.type)) return state;
      return withMutation(state, (present) => {
        if (!present.wdw) return present;
        const next = clonePreset(present);
        const blocks = next.wdw?.[action.lane].blocks;
        if (!blocks) return present;
        blocks.splice(clamp(Math.trunc(action.index), 0, blocks.length), 0, block);
        return next;
      }, block.id);
    }
    case "move-lane-block": {
      const rig = findPresetBlock(state.history.present.blocks, action.rigId);
      if (!rig?.lanes) return state;
      const sourceLane = rig.lanes.left.blocks.some(({ id }) => id === action.blockId)
        ? "left"
        : rig.lanes.right.blocks.some(({ id }) => id === action.blockId) ? "right" : undefined;
      if (!sourceLane) return state;
      return withMutation(state, (present) => updatedBlock(present, action.rigId, (candidate) => {
        const next = structuredClone(candidate);
        if (!next.lanes) return candidate;
        const source = next.lanes[sourceLane].blocks;
        const sourceIndex = source.findIndex(({ id }) => id === action.blockId);
        if (sourceIndex < 0) return candidate;
        const [block] = source.splice(sourceIndex, 1);
        const target = next.lanes[action.lane].blocks;
        const destination = clamp(Math.trunc(action.index), 0, target.length);
        target.splice(destination, 0, block);
        return next;
      }), action.blockId);
    }
    case "move-wdw-block": {
      const routing = state.history.present.wdw;
      if (state.history.present.routing !== "wdw" || !routing) return state;
      const sourceLane = routing.dry.blocks.some(({ id }) => id === action.blockId)
        ? "dry" : routing.wet.blocks.some(({ id }) => id === action.blockId) ? "wet" : undefined;
      if (!sourceLane) return state;
      const sourceBlock = routing[sourceLane].blocks.find(({ id }) => id === action.blockId);
      if (!sourceBlock || !isWdwBlockAllowed(action.lane, sourceBlock)) return state;
      const target = routing[action.lane].blocks;
      if (sourceLane !== action.lane && target.length >= 10) return state;
      return withMutation(state, (present) => {
        if (!present.wdw) return present;
        const next = clonePreset(present);
        const source = next.wdw?.[sourceLane].blocks;
        const target = next.wdw?.[action.lane].blocks;
        if (!source || !target) return present;
        const sourceIndex = source.findIndex(({ id }) => id === action.blockId);
        if (sourceIndex < 0) return present;
        const [block] = source.splice(sourceIndex, 1);
        target.splice(clamp(Math.trunc(action.index), 0, target.length), 0, block);
        return next;
      }, action.blockId);
    }
    case "toggle-block":
      return withMutation(state, (present) => updatedBlock(present, action.blockId, (block) => ({
        ...block, enabled: action.enabled,
      })));
    case "duplicate-block": {
      const source = findPresetBlockInPreset(state.history.present, action.blockId);
      if (!source || source.type === "dualRig") return state;
      const sourceLane = wdwLaneForBlock(state.history.present, action.blockId);
      if (sourceLane && (source.type === "nam" || source.type === "cab")
          && state.history.present.wdw![sourceLane].blocks.some(({ id, type }) =>
            id !== source.id && type === source.type)) return state;
      const id = nextPresetBlockId(allPresetBlocksInPreset(state.history.present));
      return withMutation(state, (present) => {
        const next = clonePreset(present);
        const duplicateIn = (blocks: PresetBlock[]): boolean => {
          const sourceIndex = blocks.findIndex(({ id: candidate }) => candidate === action.blockId);
          if (sourceIndex >= 0) {
            if (blocks.length >= 10) return false;
            blocks.splice(sourceIndex + 1, 0, { ...structuredClone(blocks[sourceIndex]), id });
            return true;
          }
          return blocks.some((block) => block.lanes
            && (duplicateIn(block.lanes.left.blocks) || duplicateIn(block.lanes.right.blocks)));
        };
        const duplicated = duplicateIn(next.blocks)
          || (next.wdw && (duplicateIn(next.wdw.dry.blocks) || duplicateIn(next.wdw.wet.blocks)));
        return duplicated ? next : undefined;
      }, id);
    }
    case "remove-block": {
      let selected: string | undefined;
      const next = clonePreset(state.history.present);
      const removeFrom = (blocks: PresetBlock[], parentId?: string): boolean => {
        const index = blocks.findIndex(({ id }) => id === action.blockId);
        if (index >= 0) {
          blocks.splice(index, 1);
          selected = parentId ?? blocks[Math.min(index, blocks.length - 1)]?.id;
          return true;
        }
        return blocks.some((block) => block.lanes
          && (removeFrom(block.lanes.left.blocks, block.id)
            || removeFrom(block.lanes.right.blocks, block.id)));
      };
      const removed = removeFrom(next.blocks)
        || (next.wdw && (removeFrom(next.wdw.dry.blocks) || removeFrom(next.wdw.wet.blocks)));
      return removed ? withMutation(state, () => next, selected) : state;
    }
    case "set-block-asset":
      return withMutation(state, (present) => updatedBlock(present, action.blockId, (block) => ({
        ...block, asset: action.asset,
      })));
    case "set-block-param":
      return setBlockParam(state, action.blockId, action.key, action.value);
    case "set-scene-bypass":
      return withMutation(state, (present) => updatedBlock(present, action.blockId, (block) => {
        if (!["delay", "reverb", "irreverb"].includes(block.type) || present.version !== 4) return block;
        return { ...block, sceneBypass: action.policy };
      }));
    case "set-eq-band":
      return setEqBand(state, action.blockId, action.band, action.patch);
    case "change-definition": {
      let target;
      try {
        target = getEffectDefinition(action.definitionId);
      } catch {
        return state;
      }
      const source = findPresetBlockInPreset(state.history.present, action.blockId);
      if (!source || source.type !== target.blockType || target.mode === undefined) return state;
      const sourceLane = wdwLaneForBlock(state.history.present, action.blockId);
      if (sourceLane && !isWdwBlockAllowed(sourceLane, target.blockType)) return state;
      if (sourceLane && (target.blockType === "nam" || target.blockType === "cab")
          && state.history.present.wdw![sourceLane].blocks.some(({ id, type }) =>
            id !== source.id && type === target.blockType)) return state;
      return withMutation(state, (present) => updatedBlock(present, action.blockId, (block) => {
        const defaults = defaultsForDefinition(target.id);
        const params = { ...defaults, ...block.params, mode: target.mode };
        return { ...block, params };
      }));
    }
    case "reset-block":
      return withMutation(state, (present) => updatedBlock(present, action.blockId, resetKnownParams));
    case "replace-present":
      return withMutation(state, () => clonePreset(action.preset));
    case "mark-saved": {
      const saved = clonePreset(action.preset);
      const selectedBlockId = state.selectedBlockId
        && findPresetBlockInPreset(saved, state.selectedBlockId) ? state.selectedBlockId : undefined;
      return {
        ...state,
        saved,
        selectedBlockId,
        history: { ...state.history, present: clonePreset(action.preset), future: [] },
      };
    }
    case "undo": {
      if (state.history.past.length === 0) return state;
      const present = state.history.past[state.history.past.length - 1];
      return {
        ...state,
        history: {
          past: state.history.past.slice(0, -1),
          present: clonePreset(present),
          future: [clonePreset(state.history.present), ...state.history.future],
        },
      };
    }
    case "redo": {
      if (state.history.future.length === 0) return state;
      const [present, ...future] = state.history.future;
      return {
        ...state,
        history: {
          past: [...state.history.past, clonePreset(state.history.present)].slice(-historyLimit),
          present: clonePreset(present),
          future,
        },
      };
    }
  }
}

export function numberControlFor(block: PresetBlock, key: string): NumberControl | undefined {
  const control = findEffectDefinition(block)?.controls.find((candidate) =>
    candidate.kind === "number" && candidate.key === key,
  );
  return control?.kind === "number" ? control : undefined;
}
