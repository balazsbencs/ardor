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
  if (!("key" in control) || !(control.key in block.params)) return undefined;
  const value = block.params[control.key];
  const field = `params.${control.key}`;
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
  const assetControl = definition.controls.find((control) => control.kind === "asset");
  if (!assetControl) return [];
  if (block.asset.length === 0) {
    return [blockWarning(block, "asset-required", `${definition.name} needs an asset before it can be applied.`, "asset")];
  }
  const available = assets[assetControl.assetKind].some(({ path }) => path === block.asset);
  return available
    ? []
    : [blockWarning(block, "asset-missing", `${definition.name} asset “${block.asset}” is not installed.`, "asset")];
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

  if (source.version !== 1) presetIssues.push(error("version", "Preset version must be 1.", "version"));
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
    const ids = new Set<string>();
    const enabledGroups = new Map<string, string>();
    let stereoEstablished = false;

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
      }

      if (block.enabled && (block.type === "nam" || block.type === "cab") && stereoEstablished) {
        blockIssues.push(blockWarning(
          block,
          "mono-after-stereo",
          `${block.type === "nam" ? "NAM" : "Cabinet"} must appear before enabled modulation, delay, or reverb blocks.`,
          "type",
        ));
      }
      if (block.enabled && (block.type === "mod" || block.type === "delay" || block.type === "reverb")) {
        stereoEstablished = true;
      }
    });
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
