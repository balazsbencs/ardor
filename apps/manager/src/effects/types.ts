export type EffectCategory =
  | "amp"
  | "cabinet"
  | "dynamics"
  | "eq"
  | "modulation"
  | "delay"
  | "reverb";

export type NumberControl = {
  kind: "number";
  key: string;
  label: string;
  minimum: number;
  maximum: number;
  step: number;
  unit: "percent" | "db" | "ms" | "hz" | "ratio" | "plain";
  defaultValue: number;
};

export type ChoiceControl = {
  kind: "choice";
  key: string;
  label: string;
  choices: Array<{ value: string; label: string }>;
  defaultValue: string;
};

export type ToggleControl = {
  kind: "toggle";
  key: string;
  label: string;
  defaultValue: boolean;
};

export type AssetControl = {
  kind: "asset";
  label: string;
  assetKind: "models" | "irs";
};

export type EqControl = { kind: "parametric-eq-5" };

export type EffectControl = NumberControl | ChoiceControl | ToggleControl | AssetControl | EqControl;

export type EffectDefinition = {
  id: string;
  blockType: string;
  mode?: string;
  name: string;
  description: string;
  category: EffectCategory;
  aliases?: string[];
  constraintGroup?: "nam" | "cab" | "mod" | "delay" | "reverb";
  maxEnabledInGroup?: number;
  controls: EffectControl[];
};

export type EffectCatalog = {
  version: 1;
  definitions: EffectDefinition[];
};
