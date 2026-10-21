import type { PresetBlock } from "../api/types";
import { useDeviceSession } from "../connection/deviceSession";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { ChoiceStrip } from "../ui/ChoiceStrip";
import { TravelScale } from "../ui/TravelScale";

/** Learned controls are shared preset values. They are restored before model prewarm. */
export function ModelParameters({ block, focusedKey, onFocusKey }: {
  block: PresetBlock; focusedKey?: string; onFocusKey(key: string): void;
}) {
  const session = useDeviceSession();
  const editor = usePresetEditorContext();
  const model = session.models.find((asset) => asset.path === block.asset)?.nam;
  if (!model) return null;
  if (!model.supported) return <p className="lb-missing" role="alert">{model.error ?? "This model architecture is not supported."}</p>;
  if (!model.parameters?.length) return null;
  const saved = block.params.modelParameters;
  const values = saved && typeof saved === "object" && !Array.isArray(saved) ? saved as Record<string, unknown> : {};
  const change = (name: string, value: number, gesture?: string) =>
    editor.editParameter(block.id, "modelParameters", { ...values, [name]: value }, gesture);
  return <>
    <h3 className="ctlgrid__lane">Model controls</h3>
    <p className="lb-note">These controls are learned by the model and shared across scenes. Apply the preset to hear changes.</p>
    {model.parameters.map((p) => {
      const value = typeof values[p.name] === "number" ? values[p.name] as number : p.default;
      const key = `model:${p.name}`;
      if (p.type === "switch") return <ChoiceStrip key={key} label={p.name} family="amp" value={String(value)}
        options={(p.enum_names ?? []).map((label, index) => ({ value: String(index), label }))}
        onChange={(next) => change(p.name, Number(next))} />;
      return <TravelScale key={key} family="amp" value={value}
        control={{ kind: "number", key, label: p.name, minimum: p.min, maximum: p.max,
          step: (p.max - p.min) / 1000, unit: "plain", defaultValue: p.default }}
        focused={focusedKey === key} onFocusControl={() => onFocusKey(key)}
        onChange={(next, gesture) => change(p.name, next, gesture)} />;
    })}
  </>;
}
