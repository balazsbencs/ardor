import type { Asset, Preset, PresetBlock } from "../../api/types";
import { allEffectDefinitions, findEffectDefinition } from "../../effects/catalog";
import type { EffectControl, EffectDefinition } from "../../effects/types";
import { isWdwBlockAllowed } from "./wdwPolicy";

export type ValidationIssue = {
  severity: "warning" | "error";
  code: string;
  message: string;
  blockId?: string;
  field?: string;
};

export type PresetValidationResult = {
  issues: ValidationIssue[];
  canSave: boolean;
  canApply: boolean;
};

export type AssetInventory = {
  models: Asset[];
  irs: Asset[];
  reverbIrs: Asset[];
};

const emptyAssets: AssetInventory = { models: [], irs: [], reverbIrs: [] };
const knownTypes = new Set(allEffectDefinitions().map(({ blockType }) => blockType));

function error(code: string, message: string, field?: string): ValidationIssue {
  return { severity: "error", code, message, field };
}

function blockError(block: PresetBlock, code: string, message: string, field?: string): ValidationIssue {
  return { severity: "error", code, message, blockId: block.id, field };
}

function blockWarning(block: PresetBlock, code: string, message: string, field?: string): ValidationIssue {
  return { severity: "warning", code, message, blockId: block.id, field };
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function validAssetPath(path: string): boolean {
  if (path.length === 0) return true;
  if (path.startsWith("/") || path.includes("\\") || /^[A-Za-z]:\//.test(path)) return false;
  const segments = path.split("/");
  return segments.every((segment) => segment.length > 0 && segment !== "." && segment !== "..");
}

function validateNumber(
  block: PresetBlock,
  field: string,
  value: unknown,
  minimum: number,
  maximum: number,
): ValidationIssue | undefined {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    return blockError(block, "parameter-type", `${field} must be a finite number.`, field);
  }
  if (value < minimum || value > maximum) {
    return blockError(block, "parameter-range", `${field} must be between ${minimum} and ${maximum}.`, field);
  }
  return undefined;
}

function validateControl(block: PresetBlock, control: EffectControl): ValidationIssue | undefined {
  const key = "key" in control && typeof control.key === "string" ? control.key : undefined;
  if (!key || !(key in block.params)) return undefined;
  const value = block.params[key];
  const field = `params.${key}`;
  if (control.kind === "number") {
    return validateNumber(block, field, value, control.minimum, control.maximum);
  }
  if (control.kind === "choice") {
    return typeof value === "string" && control.choices.some(({ value: choice }) => choice === value)
      ? undefined
      : blockError(block, "parameter-type", `${control.label} must be one of its supported choices.`, field);
  }
  if (control.kind === "toggle") {
    return typeof value === "boolean"
      ? undefined
      : blockError(block, "parameter-type", `${control.label} must be on or off.`, field);
  }
  return undefined;
}

function validateEq(block: PresetBlock): ValidationIssue[] {
  const issues: ValidationIssue[] = [];
  for (const [key, label] of [["high_pass", "High-pass"], ["low_pass", "Low-pass"]] as const) {
    const filter = block.params[key];
    if (filter === undefined) continue;
    if (!isRecord(filter)) {
      issues.push(blockError(block, "parameter-type", `${label} filter must be an object.`, `params.${key}`));
      continue;
    }
    if (typeof filter.enabled !== "boolean") {
      issues.push(blockError(block, "parameter-type", `${label} enabled must be boolean.`, `params.${key}.enabled`));
    }
    for (const [field, minimum, maximum] of [
      ["frequency_hz", 20, 20000], ["q", 0.1, 18],
    ] as const) {
      const issue = validateNumber(block, `params.${key}.${field}`, filter[field], minimum, maximum);
      if (issue) issues.push(issue);
    }
    if (filter.slope_db_per_octave !== undefined
        && ![6, 12, 18, 24].includes(filter.slope_db_per_octave as number)) {
      issues.push(blockError(block, "parameter-range",
        `${label} slope must be 6, 12, 18, or 24 dB/oct.`,
        `params.${key}.slope_db_per_octave`));
    }
  }
  const bands = block.params.bands;
  if (!Array.isArray(bands) || bands.length !== 5) {
    return [blockError(block, "eq-band-count", "Five Band EQ must contain exactly five bands.", "params.bands")];
  }
  for (let index = 0; index < bands.length; index += 1) {
    const band = bands[index];
    const prefix = `params.bands.${index}`;
    if (!isRecord(band)) {
      issues.push(blockError(block, "parameter-type", `EQ band ${index + 1} must be an object.`, prefix));
      continue;
    }
    if (typeof band.enabled !== "boolean") {
      issues.push(blockError(block, "parameter-type", `EQ band ${index + 1} enabled must be boolean.`, `${prefix}.enabled`));
    }
    for (const [key, minimum, maximum] of [
      ["frequency_hz", 20, 20000], ["q", 0.1, 18], ["gain_db", -18, 18],
    ] as const) {
      const issue = validateNumber(block, `${prefix}.${key}`, band[key], minimum, maximum);
      if (issue) issues.push(issue);
    }
  }
  return issues;
}

function definitionForValidation(block: PresetBlock): EffectDefinition | undefined {
  return findEffectDefinition(block);
}

function assetIssues(block: PresetBlock, definition: EffectDefinition, assets: AssetInventory): ValidationIssue[] {
  const issues: ValidationIssue[] = [];
  const available = {
    models: assets.models,
    irs: assets.irs,
    // Convolution reverb used the cabinet inventory before the directories
    // were split. Keep installed legacy paths valid without offering cabinet
    // files for new reverb selections.
    "reverb-irs": [...assets.reverbIrs, ...assets.irs],
  } as const;
  for (const control of definition.controls) {
    if (control.kind !== "asset") continue;
    const field = control.key ? `params.${control.key}` : "asset";
    const value = control.key ? block.params[control.key] : block.asset;
    if (typeof value !== "string") {
      issues.push(blockError(block, "parameter-type", `${control.label} must be an asset path.`, field));
      continue;
    }
    if (!validAssetPath(value)) {
      // The generic block-level check owns the legacy unkeyed `asset` field.
      // Keyed assets (such as the four files inside Dual Amp) need their own
      // field-specific validation here.
      if (control.key) {
        issues.push(blockError(block, "asset-path", "Asset paths must be relative and cannot contain backslashes, . or .. segments.", field));
      }
      continue;
    }
    // A bypassed asset block is intentionally absent from the runtime chain.
    // Keep validating its path for safety, but do not make a draft
    // unappliable because the optional asset is empty or not installed.
    if (!block.enabled) continue;
    if (value.length === 0) {
      issues.push(blockWarning(block, "asset-required", `${control.label} is required before this block can be applied.`, field));
      continue;
    }
    if (!available[control.assetKind].some(({ path }) => path === value)) {
      issues.push(blockWarning(block, "asset-missing", `${control.label} “${value}” is not installed.`, field));
    }
  }
  return issues;
}

function structurallyValidBlock(value: unknown, index: number): value is PresetBlock {
  return isRecord(value)
    && typeof value.id === "string"
    && typeof value.type === "string"
    && typeof value.enabled === "boolean"
    && typeof value.asset === "string"
    && isRecord(value.params)
    && Number.isInteger(index);
}

function validateWdwLane(
  laneName: "dry" | "wet",
  laneValue: unknown,
  assets: AssetInventory,
  ids: Set<string>,
): ValidationIssue[] {
  const issues: ValidationIssue[] = [];
  if (!isRecord(laneValue) || !Array.isArray(laneValue.blocks)) {
    return [error("wdw-lane-shape", `WDW ${laneName} lane must contain a blocks array.`, `wdw.${laneName}.blocks`)];
  }
  const lane = laneValue as Record<string, unknown>;
  const blocks = lane.blocks as unknown[];
  const levelDb = lane.levelDb ?? 0;
  const pan = lane.pan ?? 0;
  const width = lane.width ?? 1;
  if (typeof levelDb !== "number" || !Number.isFinite(levelDb) || levelDb < -60 || levelDb > 12) {
    issues.push(error("wdw-level-range", `WDW ${laneName} level must be between -60 and 12 dB.`, `wdw.${laneName}.levelDb`));
  }
  if (laneName === "dry" && (typeof pan !== "number" || !Number.isFinite(pan) || pan < -1 || pan > 1)) {
    issues.push(error("wdw-pan-range", `WDW ${laneName} pan must be between -1 and 1.`, `wdw.${laneName}.pan`));
  }
  if (laneName === "wet" && lane.pan !== undefined
      && (typeof lane.pan !== "number" || !Number.isFinite(lane.pan) || lane.pan !== 0)) {
    issues.push(error("wdw-wet-pan", "The WDW wet lane stays stereo; use width instead of pan.", "wdw.wet.pan"));
  }
  if (laneName === "wet" && (typeof width !== "number" || !Number.isFinite(width) || width < 0 || width > 1)) {
    issues.push(error("wdw-width-range", `WDW ${laneName} width must be between 0 and 1.`, `wdw.${laneName}.width`));
  }
  if (laneName === "dry" && lane.width !== undefined
      && (typeof lane.width !== "number" || !Number.isFinite(lane.width) || lane.width !== 1)) {
    issues.push(error("wdw-dry-width", "The WDW dry lane is mono; use pan instead of width.", "wdw.dry.width"));
  }
  if (lane.enabled !== undefined && typeof lane.enabled !== "boolean") {
    issues.push(error("wdw-enabled-type", `WDW ${laneName} enabled must be boolean.`, `wdw.${laneName}.enabled`));
  }
  if (blocks.length > 10) {
    issues.push(error("wdw-lane-limit", `WDW ${laneName} lane can contain at most ten blocks.`, `wdw.${laneName}.blocks`));
  }
  let namCount = 0;
  let cabCount = 0;
  let namIndex = -1;
  let cabIndex = -1;
  blocks.forEach((value, index) => {
    if (!structurallyValidBlock(value, index)) {
      issues.push(error("block-shape", `WDW ${laneName} block ${index + 1} has an invalid shape.`, `wdw.${laneName}.blocks.${index}`));
      return;
    }
    const block = value;
    if (block.id.length === 0 || block.id.length > 80) {
      issues.push(blockError(block, "block-id-length", "Block ID must be between 1 and 80 characters.", "id"));
    }
    if (ids.has(block.id)) issues.push(blockError(block, "block-id-duplicate", `Block ID “${block.id}” is duplicated.`, "id"));
    ids.add(block.id);
    if (!validAssetPath(block.asset)) {
      issues.push(blockError(block, "asset-path", "Asset paths must be relative and cannot contain backslashes, . or .. segments.", "asset"));
    }
    if (block.type === "dualRig" || block.type === "dualAmp") {
      issues.push(blockError(block, "nested-split", "WDW lanes cannot contain another split block.", "type"));
      return;
    }
    if (!isWdwBlockAllowed(laneName, block.type)) {
      issues.push(blockWarning(block, "wdw-placement", `${block.type} is not admitted on the WDW ${laneName} lane.`, "type"));
    }
    if (!block.enabled && block.type === "nam") {
      issues.push(blockError(block, "wdw-required-disabled",
        `The WDW ${laneName} lane requires its NAM block to stay enabled.`, "enabled"));
    }
    if (block.enabled && block.type === "nam") { namCount += 1; namIndex = index; }
    if (block.enabled && block.type === "cab") { cabCount += 1; cabIndex = index; }
    const definition = definitionForValidation(block);
    if (!definition) {
      issues.push(blockWarning(block, knownTypes.has(block.type) ? "mode-unsupported" : "block-unsupported",
        `Block type “${block.type}” is not supported by this manager.`, "type"));
      return;
    }
    for (const control of definition.controls) {
      const issue = validateControl(block, control);
      if (issue) issues.push(issue);
    }
    if (definition.id === "eq:parametric_eq_5") issues.push(...validateEq(block));
    issues.push(...assetIssues(block, definition, assets));
  });
  if (namCount !== 1) issues.push({ severity: "warning", code: "wdw-nam-count", message: `WDW ${laneName} lane requires exactly one enabled NAM block.`, field: `wdw.${laneName}.blocks` });
  if (cabCount > 1) issues.push({ severity: "warning", code: "wdw-cab-count", message: `WDW ${laneName} lane supports at most one enabled cabinet block.`, field: `wdw.${laneName}.blocks` });
  if (cabIndex >= 0 && namIndex > cabIndex) {
    issues.push({ severity: "warning", code: "wdw-nam-order", message: `NAM must precede the cabinet on the WDW ${laneName} lane.`, field: `wdw.${laneName}.blocks` });
  }
  blocks.forEach((value, index) => {
    if (!structurallyValidBlock(value, index) || !value.enabled) return;
    if (["mod", "delay", "reverb", "irreverb", "stereo"].includes(value.type)) {
      if (namIndex < 0 || index < namIndex) {
        issues.push(blockWarning(value, "nam-required-first", "NAM must precede time-based effects on this lane.", "type"));
      } else if (cabIndex >= 0 && index < cabIndex) {
        issues.push(blockWarning(value, "cab-required-first", "Cabinet must precede time-based effects when a cabinet is used on this lane.", "type"));
      }
    }
  });
  return issues;
}

function collectSceneBlockTypes(source: Record<string, unknown>): Map<string, string> {
  const result = new Map<string, string>();
  const visit = (values: unknown): void => {
    if (!Array.isArray(values)) return;
    values.forEach((value) => {
      if (!isRecord(value)) return;
      if (typeof value.id === "string" && typeof value.type === "string") result.set(value.id, value.type);
      if (isRecord(value.lanes)) {
        for (const laneName of ["left", "right"] as const) {
          const lane = value.lanes[laneName];
          if (isRecord(lane)) visit(lane.blocks);
        }
      }
    });
  };
  visit(source.blocks);
  if (isRecord(source.wdw)) {
    for (const laneName of ["dry", "wet"] as const) {
      const lane = source.wdw[laneName];
      if (isRecord(lane)) visit(lane.blocks);
    }
  }
  return result;
}

function validateSceneBypassPolicies(source: Record<string, unknown>): ValidationIssue[] {
  const issues: ValidationIssue[] = [];
  const visit = (values: unknown): void => {
    if (!Array.isArray(values)) return;
    values.forEach((value) => {
      if (!isRecord(value)) return;
      const policy = value.sceneBypass;
      if (policy !== undefined && policy !== "cut" && policy !== "letRing") {
        issues.push(error("scene-bypass-policy", "Scene bypass must be cut or letRing.", "sceneBypass"));
      } else if (policy === "letRing" && (source.version !== 4
          || (value.type !== "delay" && value.type !== "reverb" && value.type !== "irreverb"))) {
        issues.push(error("scene-bypass-policy",
          "Let ring requires a version 4 delay or reverb block.", "sceneBypass"));
      }
      if (isRecord(value.lanes)) {
        for (const laneName of ["left", "right"] as const) {
          const lane = value.lanes[laneName];
          if (isRecord(lane)) visit(lane.blocks);
        }
      }
    });
  };
  visit(source.blocks);
  if (isRecord(source.wdw)) {
    for (const laneName of ["dry", "wet"] as const) {
      const lane = source.wdw[laneName];
      if (isRecord(lane)) visit(lane.blocks);
    }
  }
  return issues;
}

function validateSceneSet(source: Record<string, unknown>, ids: Set<string>,
  blockTypes: Map<string, string>): ValidationIssue[] {
  const issues: ValidationIssue[] = [];
  const value = source.sceneSet;
  if (source.version === 4 && !isRecord(value)) {
    return [error("scene-set-required", "Preset version 4 requires a scene set.", "sceneSet")];
  }
  if (source.version !== 4 && value !== undefined && value !== null) {
    return [error("scene-version", "Scenes require preset version 4.", "version")];
  }
  if (!isRecord(value)) return issues;
  if (value.openIn !== "presets" && value.openIn !== "scenes") {
    issues.push(error("scene-open-mode", "Scene open mode must be presets or scenes.", "sceneSet.openIn"));
  }
  if (!Array.isArray(value.scenes) || value.scenes.length !== 4) {
    issues.push(error("scene-count", "A scene set must contain exactly four scenes.", "sceneSet.scenes"));
    return issues;
  }
  const sceneIds = new Set<string>();
  let expectedAddresses: Set<string> | undefined;
  value.scenes.forEach((sceneValue, sceneIndex) => {
    const field = `sceneSet.scenes.${sceneIndex}`;
    if (!isRecord(sceneValue)) {
      issues.push(error("scene-shape", "Each scene must be an object.", field));
      return;
    }
    if (typeof sceneValue.id !== "string" || !/^[A-Za-z0-9_-]{1,64}$/.test(sceneValue.id)) {
      issues.push(error("scene-id", "Scene IDs must be 1–64 letters, numbers, underscores, or hyphens.", `${field}.id`));
    } else if (sceneIds.has(sceneValue.id)) {
      issues.push(error("scene-id-duplicate", `Scene ID “${sceneValue.id}” is duplicated.`, `${field}.id`));
    } else {
      sceneIds.add(sceneValue.id);
    }
    const codePoints = typeof sceneValue.name === "string" ? [...sceneValue.name].length : 0;
    if (typeof sceneValue.name !== "string" || sceneValue.name.trim() !== sceneValue.name
        || codePoints < 1 || codePoints > 24 || /[\p{Cc}]/u.test(sceneValue.name)) {
      issues.push(error("scene-name", "Scene names must contain 1–24 characters with no surrounding whitespace or controls.", `${field}.name`));
    }
    if (typeof sceneValue.enterTimeMs !== "number" || !Number.isInteger(sceneValue.enterTimeMs)
        || (sceneValue.enterTimeMs !== 0
          && (sceneValue.enterTimeMs < 100 || sceneValue.enterTimeMs > 10_000 || sceneValue.enterTimeMs % 100 !== 0))) {
      issues.push(error("scene-enter-time", "Enter time must be Instant or 0.1–10.0 seconds in 0.1-second steps.", `${field}.enterTimeMs`));
    }
    if (typeof sceneValue.outputTrimDb !== "number" || !Number.isFinite(sceneValue.outputTrimDb)
        || sceneValue.outputTrimDb < -12 || sceneValue.outputTrimDb > 6) {
      issues.push(error("scene-trim", "Scene trim must be between −12 and +6 dB.", `${field}.outputTrimDb`));
    }
    if (!Array.isArray(sceneValue.targets) || sceneValue.targets.length > 512) {
      issues.push(error("scene-targets", "Scene targets must be an array with at most 512 entries.", `${field}.targets`));
      return;
    }
    const addresses = new Set<string>();
    sceneValue.targets.forEach((targetValue, targetIndex) => {
      const targetField = `${field}.targets.${targetIndex}`;
      if (!isRecord(targetValue) || !("value" in targetValue)) {
        issues.push(error("scene-target-shape", "Each scene target must be an object with a value.", targetField));
        return;
      }
      let address: string | undefined;
      if (targetValue.target === "inputGainDb") {
        if (typeof targetValue.value !== "number" || !Number.isFinite(targetValue.value)
            || targetValue.blockId !== undefined || targetValue.parameter !== undefined || targetValue.lane !== undefined) {
          issues.push(error("scene-target-shape", "Input gain scene targets require one finite numeric value.", targetField));
        } else address = "inputGainDb";
      } else if (targetValue.target === "parameter") {
        if (typeof targetValue.blockId !== "string" || !ids.has(targetValue.blockId)
            || typeof targetValue.parameter !== "string" || targetValue.parameter.length === 0
            || typeof targetValue.value !== "number" || !Number.isFinite(targetValue.value)
            || targetValue.lane !== undefined) {
          issues.push(error("scene-target-shape", "Parameter scene targets require an existing block, parameter, and finite value.", targetField));
        } else address = `parameter\u001f${targetValue.blockId}\u001f${targetValue.parameter}`;
      } else if (targetValue.target === "blockEnabled") {
        const sceneBypassTypes = new Set(["mod", "delay", "reverb", "irreverb", "stereo", "dynamics", "distortion", "wah", "eq"]);
        if (typeof targetValue.blockId !== "string" || !ids.has(targetValue.blockId)
            || typeof targetValue.value !== "boolean"
            || targetValue.parameter !== undefined || targetValue.lane !== undefined) {
          issues.push(error("scene-target-shape", "Block scene targets require an existing block and on/off value.", targetField));
        } else if (!sceneBypassTypes.has(blockTypes.get(targetValue.blockId) ?? "")) {
          issues.push(error("scene-bypass-shared", "This structural block enable is shared by all scenes.", targetField));
        } else address = `blockEnabled\u001f${targetValue.blockId}`;
      } else if (targetValue.target === "wdwLane") {
        const lane = targetValue.lane;
        const parameter = targetValue.parameter;
        const parameterValid = parameter === "levelDb" || parameter === "enabled"
          || (lane === "dry" && parameter === "pan")
          || (lane === "wet" && parameter === "width");
        const valueValid = parameter === "enabled"
          ? typeof targetValue.value === "boolean"
          : typeof targetValue.value === "number" && Number.isFinite(targetValue.value);
        if (source.routing !== "wdw" || (lane !== "dry" && lane !== "wet")
            || !parameterValid || !valueValid || targetValue.blockId !== undefined) {
          issues.push(error("scene-target-shape", "WDW scene targets require a valid lane control and matching value.", targetField));
        } else address = `wdwLane\u001f${lane}\u001f${parameter}`;
      } else {
        issues.push(error("scene-target-shape", "Unknown scene target type.", targetField));
      }
      if (!address) return;
      if (addresses.has(address)) {
        issues.push(error("scene-target-duplicate", "Scene target addresses must be unique.", targetField));
      }
      addresses.add(address);
    });
    if (!expectedAddresses) expectedAddresses = addresses;
    else if (addresses.size !== expectedAddresses.size
        || [...addresses].some((address) => !expectedAddresses?.has(address))) {
      issues.push(error("scene-target-mismatch", "All four scenes must define the same target addresses.", `${field}.targets`));
    }
  });
  if (typeof value.defaultSceneId !== "string" || !sceneIds.has(value.defaultSceneId)) {
    issues.push(error("scene-default", "Default scene must reference one of the four scenes.", "sceneSet.defaultSceneId"));
  }
  return issues;
}

export function validatePreset(preset: Preset, assets: AssetInventory = emptyAssets): PresetValidationResult {
  const presetIssues: ValidationIssue[] = [];
  const issuesByBlock: ValidationIssue[][] = [];
  const source = preset as unknown as Record<string, unknown>;
  const ids = new Set<string>();

  if (source.version !== 1 && source.version !== 2 && source.version !== 3 && source.version !== 4) {
    presetIssues.push(error("version", "Preset version must be 1, 2, 3, or 4.", "version"));
  }
  if (source.routing !== "serial" && source.routing !== "wdw") {
    presetIssues.push(error("routing", "Preset routing must be serial or wdw.", "routing"));
  }
  if (source.routing === "wdw" && source.version !== 3 && source.version !== 4) {
    presetIssues.push(error("wdw-version", "Wet/dry/wet routing requires preset version 3 or 4.", "version"));
  }
  if (source.routing === "serial" && source.version === 3) {
    presetIssues.push(error("version", "Preset version 3 is reserved for wet/dry/wet routing.", "version"));
  }
  if (typeof source.name !== "string") {
    presetIssues.push(error("name-type", "Preset name must be text.", "name"));
  } else if (source.name.length > 120) {
    presetIssues.push(error("name-length", "Preset name must be 120 characters or fewer.", "name"));
  }

  const global = source.global;
  if (!isRecord(global)) {
    presetIssues.push(error("global-shape", "Preset globals must be an object.", "global"));
  } else {
    for (const [key, minimum, maximum] of [
      ["inputGainDb", -60, 24], ["outputGainDb", -60, 24], ["safetyLimitDb", -60, 0],
    ] as const) {
      const value = global[key];
      if (typeof value !== "number" || !Number.isFinite(value)) {
        presetIssues.push(error("global-non-finite", `${key} must be a finite number.`, `global.${key}`));
      } else if (value < minimum || value > maximum) {
        presetIssues.push(error("global-range", `${key} must be between ${minimum} and ${maximum}.`, `global.${key}`));
      }
    }
  }

  if (!Array.isArray(source.blocks)) {
    presetIssues.push(error("blocks-shape", "Preset blocks must be an array.", "blocks"));
  } else {
    if (source.routing === "wdw") {
      if (source.blocks.length > 0) presetIssues.push(error("wdw-top-level-blocks", "WDW presets keep top-level blocks empty.", "blocks"));
      const wdw = source.wdw;
      if (!isRecord(wdw)) {
        presetIssues.push(error("wdw-shape", "WDW presets require dry and wet lane objects.", "wdw"));
      } else {
        presetIssues.push(...validateWdwLane("dry", wdw.dry, assets, ids));
        presetIssues.push(...validateWdwLane("wet", wdw.wet, assets, ids));
      }
    } else {
    if (source.blocks.length > 10) presetIssues.push(error("block-limit", "A preset can contain at most ten blocks.", "blocks"));
    const enabledGroups = new Map<string, string>();
    let enabledParallelRig: string | undefined;
    const enabledStandaloneAmpBlocks: string[] = [];
    let stereoEstablished = false;

    const validateLane = (
      rig: PresetBlock,
      laneName: "left" | "right",
      laneValue: unknown,
    ): ValidationIssue[] => {
      const laneIssues: ValidationIssue[] = [];
      if (!isRecord(laneValue) || !Array.isArray(laneValue.blocks)) {
        return [blockError(rig, "dual-rig-lane-shape",
          `Dual Rig ${laneName} lane must contain a blocks array.`, `lanes.${laneName}.blocks`)];
      }
      if (laneValue.blocks.length === 0) {
        laneIssues.push(blockError(rig, "dual-rig-lane-empty",
          `Dual Rig ${laneName} lane must contain at least one block.`, `lanes.${laneName}.blocks`));
      }
      if (laneValue.blocks.length > 10) {
        laneIssues.push(blockError(rig, "dual-rig-lane-limit",
          `Dual Rig ${laneName} lane can contain at most ten blocks.`, `lanes.${laneName}.blocks`));
      }
      const enabledGroups = new Map<string, string>();
      let laneStereo = false;
      laneValue.blocks.forEach((value, laneIndex) => {
        if (!structurallyValidBlock(value, laneIndex)) {
          laneIssues.push(blockError(rig, "block-shape",
            `${laneName} lane block ${laneIndex + 1} has an invalid shape.`,
            `lanes.${laneName}.blocks.${laneIndex}`));
          return;
        }
        const block = value;
        if (ids.has(block.id)) {
          laneIssues.push(blockError(block, "block-id-duplicate", `Block ID “${block.id}” is duplicated.`, "id"));
        }
        ids.add(block.id);
        if (block.type === "dualRig" || block.type === "dualAmp") {
          laneIssues.push(blockError(block, "nested-split",
            "Dual Rig lanes cannot contain another split block.", "type"));
          return;
        }
        if (!validAssetPath(block.asset)) {
          laneIssues.push(blockError(block, "asset-path",
            "Asset paths must be relative and cannot contain backslashes, . or .. segments.", "asset"));
        }
        const definition = definitionForValidation(block);
        if (!definition) {
          laneIssues.push(blockWarning(block,
            knownTypes.has(block.type) ? "mode-unsupported" : "block-unsupported",
            `Block type “${block.type}” is not supported in a Dual Rig lane.`, "type"));
          return;
        }
        for (const control of definition.controls) {
          const issue = validateControl(block, control);
          if (issue) laneIssues.push(issue);
        }
        if (definition.id === "eq:parametric_eq_5") laneIssues.push(...validateEq(block));
        laneIssues.push(...assetIssues(block, definition, assets));
        if (block.enabled && definition.constraintGroup && definition.maxEnabledInGroup === 1) {
          const prior = enabledGroups.get(definition.constraintGroup);
          if (prior) {
            laneIssues.push(blockWarning(block, "constraint-duplicate",
              `Only one enabled ${definition.constraintGroup} block is supported in the ${laneName} lane; disable this block or ${prior}.`,
              "enabled"));
          } else {
            enabledGroups.set(definition.constraintGroup, block.id);
          }
        }
        if (block.enabled && block.type === "cab" && laneStereo) {
          laneIssues.push(blockWarning(block, "mono-after-stereo",
            `Cabinet must precede stereo effects in the ${laneName} lane.`, "type"));
        }
        if (block.enabled && block.type === "nam") laneStereo = false;
        else if (block.enabled && (block.type === "mod" || block.type === "delay" || block.type === "reverb")) {
          laneStereo = true;
        }
      });
      return laneIssues;
    };

    source.blocks.forEach((value, index) => {
      const blockIssues: ValidationIssue[] = [];
      issuesByBlock[index] = blockIssues;
      if (!structurallyValidBlock(value, index)) {
        const blockId = isRecord(value) && typeof value.id === "string" ? value.id : undefined;
        blockIssues.push({
          severity: "error",
          code: "block-shape",
          message: `Block ${index + 1} must have string id/type/asset, boolean enabled, and object params.`,
          blockId,
          field: `blocks.${index}`,
        });
        return;
      }
      const block = value;
      if (block.id.length === 0) blockIssues.push(blockError(block, "block-id-empty", "Block ID cannot be empty.", "id"));
      if (block.id.length > 80) blockIssues.push(blockError(block, "block-id-length", "Block ID must be 80 characters or fewer.", "id"));
      if (ids.has(block.id)) blockIssues.push(blockError(block, "block-id-duplicate", `Block ID “${block.id}” is duplicated.`, "id"));
      ids.add(block.id);
      if (!validAssetPath(block.asset)) {
        blockIssues.push(blockError(block, "asset-path", "Asset paths must be relative and cannot contain backslashes, . or .. segments.", "asset"));
      }

      const definition = definitionForValidation(block);
      if (!definition) {
        blockIssues.push(blockWarning(
          block,
          knownTypes.has(block.type) ? "mode-unsupported" : "block-unsupported",
          knownTypes.has(block.type)
            ? `The ${block.type} mode is not supported by this manager.`
            : `Block type “${block.type}” is not supported by this manager.`,
          "type",
        ));
      } else {
        for (const control of definition.controls) {
          const issue = validateControl(block, control);
          if (issue) blockIssues.push(issue);
        }
        if (definition.id === "eq:parametric_eq_5") blockIssues.push(...validateEq(block));
        blockIssues.push(...assetIssues(block, definition, assets));
        if (block.type === "dualRig") {
          if (source.version !== 2 && source.version !== 4) {
            blockIssues.push(blockError(block, "dual-rig-version",
              "Dual Rig requires preset version 2 or 4.", "type"));
          }
          if (!isRecord(block.lanes)) {
            blockIssues.push(blockError(block, "dual-rig-lanes",
              "Dual Rig must contain left and right lanes.", "lanes"));
          } else {
            blockIssues.push(...validateLane(block, "left", block.lanes.left));
            blockIssues.push(...validateLane(block, "right", block.lanes.right));
          }
        }
        if (block.enabled && definition.constraintGroup && definition.maxEnabledInGroup === 1) {
          const prior = enabledGroups.get(definition.constraintGroup);
          if (prior) {
            blockIssues.push(blockWarning(
              block,
              "constraint-duplicate",
              `Only one enabled ${definition.constraintGroup} block is supported; disable this block or ${prior}.`,
              "enabled",
            ));
          } else {
            enabledGroups.set(definition.constraintGroup, block.id);
          }
        }
        if (block.enabled && (block.type === "dualAmp" || block.type === "dualRig")) {
          if (enabledParallelRig) {
            blockIssues.push(blockWarning(
              block,
              "dual-amp-conflict",
              `Only one parallel rig can be enabled; disable ${enabledParallelRig}.`,
              "enabled",
            ));
          } else if (enabledStandaloneAmpBlocks[0]) {
            blockIssues.push(blockWarning(
              block,
              "dual-amp-conflict",
              `Dual Amp cannot be combined with enabled standalone NAM or cabinet blocks; disable ${enabledStandaloneAmpBlocks[0]}.`,
              "enabled",
            ));
          } else {
            enabledParallelRig = block.id;
          }
        } else if (block.enabled && (block.type === "nam" || block.type === "cab")) {
          if (enabledParallelRig) {
            blockIssues.push(blockWarning(
              block,
              "dual-amp-conflict",
              `Standalone NAM and cabinet blocks cannot be combined with a parallel rig; disable ${enabledParallelRig}.`,
              "enabled",
            ));
          }
          enabledStandaloneAmpBlocks.push(block.id);
        }
      }

      if (block.enabled && block.type === "cab" && stereoEstablished) {
        blockIssues.push(blockWarning(
          block,
          "mono-after-stereo",
          "Cabinet must appear before enabled modulation, delay, or reverb blocks.",
          "type",
        ));
      }
      if (block.enabled && block.type === "nam") {
        stereoEstablished = false;
      } else if (block.enabled && (block.type === "dualAmp" || block.type === "dualRig")) {
        stereoEstablished = true;
      } else if (block.enabled && (block.type === "mod" || block.type === "delay" || block.type === "reverb")) {
        stereoEstablished = true;
      }
    });
    }
  }

  if (source.expression !== undefined && source.expression !== null) {
    const expression = source.expression;
    if (!isRecord(expression)
        || typeof expression.blockId !== "string"
        || typeof expression.parameter !== "string"
        || typeof expression.minimum !== "number"
        || typeof expression.maximum !== "number"
        || typeof expression.inverted !== "boolean") {
      presetIssues.push(error(
        "expression-shape",
        "Expression assignment must contain blockId, parameter, minimum, maximum, and inverted.",
        "expression",
      ));
    } else if (!expression.blockId || !expression.parameter) {
      presetIssues.push(error(
        "expression-target",
        "Expression assignment requires a block and parameter.",
        "expression",
      ));
    } else if (!ids.has(expression.blockId)) {
      presetIssues.push(error(
        "expression-block",
        `Expression target block “${expression.blockId}” does not exist.`,
        "expression.blockId",
      ));
    } else if (!Number.isFinite(expression.minimum)
        || !Number.isFinite(expression.maximum)
        || expression.minimum > expression.maximum) {
      presetIssues.push(error(
        "expression-range",
        "Expression minimum and maximum must be finite, with minimum no greater than maximum.",
        "expression",
      ));
    }
  }

  const occupiedMidi: Array<{ channel: number; controlChange: number }> = [];
  let midiActionCount = 0;
  if (source.midiMappings !== undefined && source.midiMappings !== null) {
    if (!Array.isArray(source.midiMappings)) {
      presetIssues.push(error("midi-shape", "MIDI mappings must be an array.", "midiMappings"));
    } else {
      source.midiMappings.forEach((mapping, mappingIndex) => {
        const field = `midiMappings.${mappingIndex}`;
        if (!isRecord(mapping)
            || typeof mapping.channel !== "number"
            || !Number.isInteger(mapping.channel) || mapping.channel < -1 || mapping.channel > 15
            || typeof mapping.controlChange !== "number"
            || !Number.isInteger(mapping.controlChange) || mapping.controlChange < 0 || mapping.controlChange > 127
            || (mapping.mode !== "continuous" && mapping.mode !== "toggle")
            || !Array.isArray(mapping.actions) || mapping.actions.length === 0) {
          presetIssues.push(error(
            "midi-binding-shape",
            "Each MIDI mapping needs a channel, CC number, mode, and at least one action.",
            field,
          ));
          return;
        }
        if (occupiedMidi.some((item) => item.controlChange === mapping.controlChange
          && (item.channel === -1 || mapping.channel === -1 || item.channel === mapping.channel))) {
          presetIssues.push(error(
            "midi-binding-overlap",
            `MIDI CC ${mapping.controlChange} overlaps another mapping on this channel.`,
            field,
          ));
        }
        occupiedMidi.push({ channel: mapping.channel, controlChange: mapping.controlChange });
        midiActionCount += mapping.actions.length;
        mapping.actions.forEach((action, actionIndex) => {
          const actionField = `${field}.actions.${actionIndex}`;
          if (!isRecord(action)
              || (action.target !== "parameter" && action.target !== "blockEnabled")
              || typeof action.blockId !== "string"
              || !ids.has(action.blockId)
              || (action.target === "parameter" && typeof action.parameter !== "string")
              || typeof action.value1 !== "number" || !Number.isFinite(action.value1)
              || typeof action.value2 !== "number" || !Number.isFinite(action.value2)) {
            presetIssues.push(error(
              "midi-action-shape",
              "MIDI actions require an existing block target and two finite values.",
              actionField,
            ));
          }
        });
      });
    }
  }

  if (source.sceneMidiMappings !== undefined && source.sceneMidiMappings !== null) {
    if (!Array.isArray(source.sceneMidiMappings)) {
      presetIssues.push(error("scene-midi-shape", "Scene MIDI mappings must be an array.", "sceneMidiMappings"));
    } else {
      const sceneIds = new Set<string>();
      if (isRecord(source.sceneSet) && Array.isArray(source.sceneSet.scenes)) {
        source.sceneSet.scenes.forEach((scene) => {
          if (isRecord(scene) && typeof scene.id === "string") sceneIds.add(scene.id);
        });
      }
      source.sceneMidiMappings.forEach((mapping, mappingIndex) => {
        const field = `sceneMidiMappings.${mappingIndex}`;
        if (!isRecord(mapping)
            || typeof mapping.channel !== "number" || !Number.isInteger(mapping.channel)
            || mapping.channel < -1 || mapping.channel > 15
            || typeof mapping.controlChange !== "number" || !Number.isInteger(mapping.controlChange)
            || mapping.controlChange < 0 || mapping.controlChange > 127
            || !["selectScene", "sceneNumber", "showPresets", "showScenes"].includes(
              typeof mapping.action === "string" ? mapping.action : "")) {
          presetIssues.push(error("scene-midi-binding-shape",
            "Each scene MIDI mapping needs a channel, CC number, and named action.", field));
          return;
        }
        if (occupiedMidi.some((item) => item.controlChange === mapping.controlChange
          && (item.channel === -1 || mapping.channel === -1 || item.channel === mapping.channel))) {
          presetIssues.push(error("midi-binding-overlap",
            `MIDI CC ${mapping.controlChange} overlaps another mapping on this channel.`, field));
        }
        occupiedMidi.push({ channel: mapping.channel, controlChange: mapping.controlChange });
        midiActionCount += 1;
        if (mapping.action === "selectScene") {
          if (typeof mapping.sceneId !== "string" || !sceneIds.has(mapping.sceneId)) {
            presetIssues.push(error("scene-midi-scene",
              "Direct scene MIDI actions must reference an existing scene ID.", `${field}.sceneId`));
          }
        } else if (mapping.sceneId !== undefined) {
          presetIssues.push(error("scene-midi-scene",
            "Only direct scene MIDI actions may contain a scene ID.", `${field}.sceneId`));
        }
      });
    }
  }
  if (midiActionCount > 256) {
    presetIssues.push(error("midi-action-limit",
      "A preset can contain at most 256 MIDI action targets.", "midiMappings"));
  }

  presetIssues.push(...validateSceneSet(source, ids, collectSceneBlockTypes(source)));
  presetIssues.push(...validateSceneBypassPolicies(source));

  const issues = [...presetIssues, ...issuesByBlock.flat()];
  const canSave = !issues.some(({ severity }) => severity === "error");
  return { issues, canSave, canApply: canSave && issues.length === 0 };
}

export function issuesForBlock(result: PresetValidationResult, blockId: string): ValidationIssue[] {
  return result.issues.filter((issue) => issue.blockId === blockId);
}

export function firstBlockingIssue(result: PresetValidationResult): ValidationIssue | undefined {
  return result.issues.find(({ severity }) => severity === "error") ?? result.issues[0];
}
