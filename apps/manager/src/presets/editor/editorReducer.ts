import type { Preset, PresetBlock } from "../../api/types";
import {
  createBlockFromDefinition,
  defaultsForDefinition,
  findEffectDefinition,
  getEffectDefinition,
} from "../../effects/catalog";
import type { NumberControl } from "../../effects/types";
import type { EditorAction, EditorState, EqBand, PresetLocation } from "./editorTypes";
import { clonePreset, nextPresetBlockId } from "./presetFactory";

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

function updatedBlock(preset: Preset, blockId: string, update: (block: PresetBlock) => PresetBlock): Preset | undefined {
  const index = preset.blocks.findIndex(({ id }) => id === blockId);
  if (index < 0) return undefined;
  const next = clonePreset(preset);
  next.blocks[index] = update(next.blocks[index]);
  return next;
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
  const block = state.history.present.blocks.find(({ id }) => id === blockId);
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
    case "toggle-block":
      return withMutation(state, (present) => updatedBlock(present, action.blockId, (block) => ({
        ...block, enabled: action.enabled,
      })));
    case "duplicate-block": {
      if (state.history.present.blocks.length >= 10) return state;
      const sourceIndex = state.history.present.blocks.findIndex(({ id }) => id === action.blockId);
      if (sourceIndex < 0) return state;
      const id = nextPresetBlockId(state.history.present.blocks);
      return withMutation(state, (present) => {
        const next = clonePreset(present);
        next.blocks.splice(sourceIndex + 1, 0, { ...structuredClone(next.blocks[sourceIndex]), id });
        return next;
      }, id);
    }
    case "remove-block": {
      const index = state.history.present.blocks.findIndex(({ id }) => id === action.blockId);
      if (index < 0) return state;
      const remaining = state.history.present.blocks.filter(({ id }) => id !== action.blockId);
      const selected = remaining.length > 0 ? remaining[Math.min(index, remaining.length - 1)].id : undefined;
      return withMutation(state, (present) => {
        const next = clonePreset(present);
        next.blocks.splice(index, 1);
        return next;
      }, selected);
    }
    case "set-block-asset":
      return withMutation(state, (present) => updatedBlock(present, action.blockId, (block) => ({
        ...block, asset: action.asset,
      })));
    case "set-block-param":
      return setBlockParam(state, action.blockId, action.key, action.value);
    case "set-eq-band":
      return setEqBand(state, action.blockId, action.band, action.patch);
    case "change-definition": {
      let target;
      try {
        target = getEffectDefinition(action.definitionId);
      } catch {
        return state;
      }
      const source = state.history.present.blocks.find(({ id }) => id === action.blockId);
      if (!source || source.type !== target.blockType || target.mode === undefined) return state;
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
        && saved.blocks.some(({ id }) => id === state.selectedBlockId) ? state.selectedBlockId : undefined;
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
