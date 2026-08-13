import type { Asset, Preset, PresetBlock } from "../../api/types";
import { allEffectDefinitions, findEffectDefinition } from "../../effects/catalog";
import type { EffectControl, EffectDefinition } from "../../effects/types";

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
};

const emptyAssets: AssetInventory = { models: [], irs: [] };
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
    if (value.length === 0) {
      issues.push(blockWarning(block, "asset-required", `${control.label} is required before this block can be applied.`, field));
      continue;
    }
    if (!assets[control.assetKind].some(({ path }) => path === value)) {
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

export function validatePreset(preset: Preset, assets: AssetInventory = emptyAssets): PresetValidationResult {
  const presetIssues: ValidationIssue[] = [];
  const issuesByBlock: ValidationIssue[][] = [];
  const source = preset as unknown as Record<string, unknown>;
  const ids = new Set<string>();

  if (source.version !== 1 && source.version !== 2) {
    presetIssues.push(error("version", "Preset version must be 1 or 2.", "version"));
  }
  if (source.routing !== "serial") presetIssues.push(error("routing", "Preset routing must be serial.", "routing"));
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
          if (source.version !== 2) {
            blockIssues.push(blockError(block, "dual-rig-version",
              "Dual Rig requires preset version 2.", "type"));
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

  if (source.midiMappings !== undefined && source.midiMappings !== null) {
    if (!Array.isArray(source.midiMappings)) {
      presetIssues.push(error("midi-shape", "MIDI mappings must be an array.", "midiMappings"));
    } else {
      const topLevelIds = new Set(preset.blocks.map(({ id }) => id));
      const occupied: Array<{ channel: number; controlChange: number }> = [];
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
        if (occupied.some((item) => item.controlChange === mapping.controlChange
          && (item.channel === -1 || mapping.channel === -1 || item.channel === mapping.channel))) {
          presetIssues.push(error(
            "midi-binding-overlap",
            `MIDI CC ${mapping.controlChange} overlaps another mapping on this channel.`,
            field,
          ));
        }
        occupied.push({ channel: mapping.channel, controlChange: mapping.controlChange });
        mapping.actions.forEach((action, actionIndex) => {
          const actionField = `${field}.actions.${actionIndex}`;
          if (!isRecord(action)
              || (action.target !== "parameter" && action.target !== "blockEnabled")
              || typeof action.blockId !== "string"
              || !topLevelIds.has(action.blockId)
              || (action.target === "parameter" && typeof action.parameter !== "string")
              || typeof action.value1 !== "number" || !Number.isFinite(action.value1)
              || typeof action.value2 !== "number" || !Number.isFinite(action.value2)) {
            presetIssues.push(error(
              "midi-action-shape",
              "MIDI actions require a top-level block target and two finite values.",
              actionField,
            ));
          }
        });
      });
    }
  }

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
