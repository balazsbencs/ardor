import type { PresetBlock } from "../api/types";
import rawCatalog from "./catalog.v1.json";
import { daisyNormalizedStep, daisyNumberDisplay, daisyParameterLabel } from "./daisyValues";
import type {
  AssetControl,
  ChoiceControl,
  EffectCatalog,
  EffectCategory,
  EffectControl,
  EffectDefinition,
  NumberControl,
  ToggleControl,
} from "./types";

const categories = new Set<EffectCategory>([
  "amp", "cabinet", "dynamics", "eq", "modulation", "delay", "reverb",
]);
const units = new Set<NumberControl["unit"]>(["percent", "db", "ms", "hz", "ratio", "plain"]);
const assetKinds = new Set<AssetControl["assetKind"]>(["models", "irs"]);
const constraintGroups = new Set<NonNullable<EffectDefinition["constraintGroup"]>>([
  "nam", "cab", "mod", "delay", "reverb",
]);

function fail(path: string, message: string): never {
  throw new Error(`Invalid effect catalog at ${path}: ${message}`);
}

function recordAt(value: unknown, path: string): Record<string, unknown> {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    fail(path, "expected an object");
  }
  return value as Record<string, unknown>;
}

function stringAt(value: unknown, path: string): string {
  if (typeof value !== "string" || value.length === 0) fail(path, "expected a non-empty string");
  return value;
}

function numberAt(value: unknown, path: string): number {
  if (typeof value !== "number" || !Number.isFinite(value)) fail(path, "expected a finite number");
  return value;
}

function booleanAt(value: unknown, path: string): boolean {
  if (typeof value !== "boolean") fail(path, "expected a boolean");
  return value;
}

function stringArrayAt(value: unknown, path: string): string[] {
  if (!Array.isArray(value)) fail(path, "expected an array");
  return value.map((entry, index) => stringAt(entry, `${path}[${index}]`));
}

function controlAt(value: unknown, path: string): EffectControl {
  const source = recordAt(value, path);
  const kind = stringAt(source.kind, `${path}.kind`);
  if (kind === "number") {
    const unit = stringAt(source.unit, `${path}.unit`);
    if (!units.has(unit as NumberControl["unit"])) fail(`${path}.unit`, `unsupported unit ${unit}`);
    const control: NumberControl = {
      kind,
      key: stringAt(source.key, `${path}.key`),
      label: stringAt(source.label, `${path}.label`),
      minimum: numberAt(source.minimum, `${path}.minimum`),
      maximum: numberAt(source.maximum, `${path}.maximum`),
      step: numberAt(source.step, `${path}.step`),
      unit: unit as NumberControl["unit"],
      defaultValue: numberAt(source.defaultValue, `${path}.defaultValue`),
    };
    if (control.minimum > control.maximum) fail(path, "minimum exceeds maximum");
    if (control.step <= 0) fail(`${path}.step`, "must be greater than zero");
    if (control.defaultValue < control.minimum || control.defaultValue > control.maximum) {
      fail(`${path}.defaultValue`, "must be within the control range");
    }
    return control;
  }
  if (kind === "choice") {
    if (!Array.isArray(source.choices) || source.choices.length === 0) fail(`${path}.choices`, "expected choices");
    const choices = source.choices.map((entry, index) => {
      const choice = recordAt(entry, `${path}.choices[${index}]`);
      return {
        value: stringAt(choice.value, `${path}.choices[${index}].value`),
        label: stringAt(choice.label, `${path}.choices[${index}].label`),
      };
    });
    const control: ChoiceControl = {
      kind,
      key: stringAt(source.key, `${path}.key`),
      label: stringAt(source.label, `${path}.label`),
      choices,
      defaultValue: stringAt(source.defaultValue, `${path}.defaultValue`),
    };
    if (!choices.some(({ value: choice }) => choice === control.defaultValue)) {
      fail(`${path}.defaultValue`, "must match a choice value");
    }
    return control;
  }
  if (kind === "toggle") {
    const control: ToggleControl = {
      kind,
      key: stringAt(source.key, `${path}.key`),
      label: stringAt(source.label, `${path}.label`),
      defaultValue: booleanAt(source.defaultValue, `${path}.defaultValue`),
    };
    return control;
  }
  if (kind === "asset") {
    const assetKind = stringAt(source.assetKind, `${path}.assetKind`);
    if (!assetKinds.has(assetKind as AssetControl["assetKind"])) {
      fail(`${path}.assetKind`, `unsupported asset kind ${assetKind}`);
    }
    return {
      kind,
      label: stringAt(source.label, `${path}.label`),
      assetKind: assetKind as AssetControl["assetKind"],
    };
  }
  if (kind === "parametric-eq-5") return { kind };
  return fail(`${path}.kind`, `unsupported control kind ${kind}`);
}

function definitionAt(value: unknown, path: string): EffectDefinition {
  const source = recordAt(value, path);
  const id = stringAt(source.id, `${path}.id`);
  const blockType = stringAt(source.blockType, `${path}.blockType`);
  const name = stringAt(source.name, `${path}.name`);
  const description = stringAt(source.description, `${path}.description`);
  const category = stringAt(source.category, `${path}.category`);
  if (!categories.has(category as EffectCategory)) fail(`${path}.category`, `unsupported category ${category}`);
  if (!Array.isArray(source.controls)) fail(`${path}.controls`, "expected an array");
  const controls = source.controls.map((control, index) => controlAt(control, `${path}.controls[${index}]`));
  if (["mod", "delay", "reverb"].includes(blockType) && typeof source.mode === "string") {
    for (const control of controls) {
      if (control.kind !== "number") continue;
      control.display = daisyNumberDisplay(blockType, source.mode, control.key);
      control.step = daisyNormalizedStep(blockType, source.mode, control.key, control.display);
      control.label = daisyParameterLabel(blockType, source.mode, control.key, control.label);
    }
  }
  const definition: EffectDefinition = {
    id,
    blockType,
    name,
    description,
    category: category as EffectCategory,
    controls,
  };
  if (source.mode !== undefined) definition.mode = stringAt(source.mode, `${path}.mode`);
  if (source.aliases !== undefined) definition.aliases = stringArrayAt(source.aliases, `${path}.aliases`);
  if (source.constraintGroup !== undefined) {
    const group = stringAt(source.constraintGroup, `${path}.constraintGroup`);
    if (!constraintGroups.has(group as NonNullable<EffectDefinition["constraintGroup"]>)) {
      fail(`${path}.constraintGroup`, `unsupported constraint group ${group}`);
    }
    definition.constraintGroup = group as NonNullable<EffectDefinition["constraintGroup"]>;
  }
  if (source.maxEnabledInGroup !== undefined) {
    const maximum = numberAt(source.maxEnabledInGroup, `${path}.maxEnabledInGroup`);
    if (!Number.isInteger(maximum) || maximum < 1) fail(`${path}.maxEnabledInGroup`, "expected a positive integer");
    definition.maxEnabledInGroup = maximum;
  }
  return definition;
}

export function validateEffectCatalog(value: unknown): EffectCatalog {
  const source = recordAt(value, "catalog");
  if (source.version !== 1) fail("version", "expected version 1");
  if (!Array.isArray(source.definitions)) fail("definitions", "expected an array");
  const definitions = source.definitions.map((definition, index) => definitionAt(definition, `definitions[${index}]`));
  const ids = new Set<string>();
  const pairs = new Set<string>();
  for (const [index, definition] of definitions.entries()) {
    if (ids.has(definition.id)) fail(`definitions[${index}].id`, `duplicate id ${definition.id}`);
    ids.add(definition.id);
    const pair = `${definition.blockType}\u0000${definition.mode ?? ""}`;
    if (pairs.has(pair)) fail(`definitions[${index}]`, "duplicate block type and mode");
    pairs.add(pair);
  }
  return { version: 1, definitions };
}

const catalog = validateEffectCatalog(rawCatalog);

export function allEffectDefinitions(): EffectDefinition[] {
  return catalog.definitions.slice();
}

export function findEffectDefinition(block: PresetBlock): EffectDefinition | undefined {
  const mode = typeof block.params.mode === "string" ? block.params.mode : undefined;
  return catalog.definitions.find((definition) =>
    definition.blockType === block.type && (definition.mode === undefined || definition.mode === mode),
  );
}

export function getEffectDefinition(id: string): EffectDefinition {
  const definition = catalog.definitions.find((candidate) => candidate.id === id);
  if (!definition) throw new Error(`Unknown effect definition: ${id}`);
  return definition;
}

export function definitionsForCategory(category: EffectCategory): EffectDefinition[] {
  return catalog.definitions.filter((definition) => definition.category === category);
}

export function defaultsForDefinition(id: string): Record<string, unknown> {
  const definition = getEffectDefinition(id);
  if (definition.id === "eq:parametric_eq_5") {
    return {
      mode: "parametric_eq_5",
      bands: [80, 250, 800, 2500, 8000].map((frequency_hz) => ({
        enabled: true,
        frequency_hz,
        q: 1,
        gain_db: 0,
      })),
    };
  }
  const defaults: Record<string, unknown> = {};
  if (definition.mode !== undefined) defaults.mode = definition.mode;
  for (const control of definition.controls) {
    if (control.kind === "number" || control.kind === "choice" || control.kind === "toggle") {
      defaults[control.key] = control.defaultValue;
    }
  }
  return defaults;
}

function nextBlockId(existingBlocks: PresetBlock[]): string {
  const used = new Set(existingBlocks.map(({ id }) => id));
  let greatest = 0;
  for (const id of used) {
    const match = /^block-([1-9]\d*)$/.exec(id);
    if (match) greatest = Math.max(greatest, Number(match[1]));
  }
  if (greatest > 0) return `block-${greatest + 1}`;
  let candidate = 1;
  while (used.has(`block-${candidate}`)) candidate += 1;
  return `block-${candidate}`;
}

export function createBlockFromDefinition(
  id: string,
  existingBlocks: PresetBlock[],
  asset?: string,
): PresetBlock {
  const definition = getEffectDefinition(id);
  return {
    id: nextBlockId(existingBlocks),
    type: definition.blockType,
    enabled: true,
    asset: definition.controls.some((control) => control.kind === "asset") ? asset ?? "" : "",
    params: defaultsForDefinition(id),
  };
}
