import type { AssetKind, PresetBlock } from "../api/types";
import type { EffectControl } from "../effects/types";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { sceneOwns } from "../presets/scenes/sceneView";
import { isSharedOnlyParameter, parameterHelp } from "../presets/scenes/sharedParameters";
import { ChoiceStrip } from "../ui/ChoiceStrip";
import { familyOf } from "../ui/family";
import { TravelScale } from "../ui/TravelScale";
import { EqEditor } from "./EqEditor";
import { FilePicker } from "./FilePicker";

type Props = { block: PresetBlock; control: EffectControl; focusedKey?: string; onFocusKey(key: string): void; onManageFiles(kind: AssetKind): void };

/** One control of a block at drawer scale, whatever its kind. */
export function DrawerControl({ block, control: c, focusedKey, onFocusKey, onManageFiles }: Props) {
  const editor = usePresetEditorContext();
  const family = familyOf(block.type);
  const scene = editor.editingScene;
  const valueOf = <T,>(key: string, fallback: T): T => (typeof block.params[key] === typeof fallback ? block.params[key] as T : fallback);

  if (c.kind === "asset") {
    const value = c.key ? String(block.params[c.key] ?? "") : block.asset;
    return <FilePicker label={c.label} kind={c.assetKind} value={value} onManage={() => onManageFiles(c.assetKind)}
      onChange={(path) => (c.key ? editor.editParameter(block.id, c.key, path) : editor.dispatch({ type: "set-block-asset", blockId: block.id, asset: path }))} />;
  }
  if (c.kind === "number") {
    const value = valueOf(c.key, c.defaultValue);
    const ownScene = scene && !isSharedOnlyParameter(block.type, c.key) ? scene : undefined;
    return <TravelScale control={c} value={value} family={family}
      focused={focusedKey === c.key} onFocusControl={() => onFocusKey(c.key)}
      description={parameterHelp(block.type, c.key, Boolean(editor.present.sceneSet))}
      owned={ownScene ? (sceneOwns(ownScene, block.id, c.key) ? "scene" : "shared") : undefined}
      onShare={ownScene ? () => editor.dispatch({ type: "set-scene-scope", sceneId: ownScene.id, blockId: block.id, parameter: c.key, scope: "shared", value }) : undefined}
      onChange={(next, gesture) => editor.editParameter(block.id, c.key, next, gesture)} />;
  }
  if (c.kind === "choice") {
    return <ChoiceStrip label={c.label} family={family} value={valueOf(c.key, c.defaultValue)} options={c.choices} onChange={(value) => editor.editParameter(block.id, c.key, value)} />;
  }
  if (c.kind === "toggle") {
    return <ChoiceStrip label={c.label} family={family} value={valueOf(c.key, c.defaultValue)}
      options={[{ value: false, label: "Off" }, { value: true, label: "On" }]} onChange={(value) => editor.editParameter(block.id, c.key, value)} />;
  }
  return <EqEditor block={block}
    onEqBand={(id, band, patch, gesture) => editor.dispatch({ type: "set-eq-band", blockId: id, band, patch, gesture })}
    onParam={(id, key, value, gesture) => editor.editParameter(id, key, value, gesture)} />;
}
