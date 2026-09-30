import { Copy, Power, RotateCcw, Trash2, X } from "lucide-react";

import type { AssetKind, PresetBlock } from "../api/types";
import { Button, IconButton } from "../components/ui";
import { allEffectDefinitions, findEffectDefinition } from "../effects/catalog";
import type { EffectControl } from "../effects/types";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import type { ValidationIssue } from "../presets/editor/presetValidation";
import { ChoiceStrip } from "../ui/ChoiceStrip";
import { capFor, familyOf } from "../ui/family";
import { sceneOwns } from "../presets/scenes/sceneView";
import { Tag } from "../ui/Tag";
import { blockTitle } from "./BlockCard";
import { DrawerControl } from "./DrawerControl";
import { SceneScope } from "./SceneScope";
import "./drawer.css";

const SCENE_BYPASS_TYPES = ["delay", "reverb", "irreverb"];

const keyOf = (control: EffectControl): string => ("key" in control && typeof control.key === "string" ? control.key : "");

export function BlockDrawer({ block, issues, focusedKey, onFocusKey, onClose, onManageFiles }: {
  block: PresetBlock; issues: ValidationIssue[]; focusedKey?: string; onFocusKey(key: string): void; onClose(): void; onManageFiles(kind: AssetKind): void;
}) {
  const editor = usePresetEditorContext();
  const definition = findEffectDefinition(block);
  const family = familyOf(block.type);
  const modes = definition?.mode ? allEffectDefinitions().filter((d) => d.blockType === definition.blockType && d.mode) : [];
  const isDual = block.type === "dualAmp" || block.type === "dualRig";

  const control = (c: EffectControl) => (
    <DrawerControl key={keyOf(c) || c.kind} block={block} control={c} focusedKey={focusedKey} onFocusKey={onFocusKey} onManageFiles={onManageFiles} />
  );
  const controls = definition?.controls ?? [];
  const general = controls.filter((c) => !keyOf(c).startsWith("left") && !keyOf(c).startsWith("right"));
  const left = controls.filter((c) => keyOf(c).startsWith("left"));
  const right = controls.filter((c) => keyOf(c).startsWith("right"));

  return (
    <section className={`drawer fam-${family}`} aria-label={`${blockTitle(block)} parameters`}>
      <div className="drawer__head">
        <Tag tone="line">{capFor(block.type)}</Tag>
        <h2>{blockTitle(block)}</h2>
        <span className="drawer__sub">{block.asset ? definition?.name : definition?.description}</span>
        <span className="drawer__actions">
          <SceneScope />
          {editor.editingScene && sceneOwns(editor.editingScene, block.id) && (
            <>
              <Tag tone="scene">SCENE</Tag>
              <button type="button" className="lb-ctl__share" title="All scenes use one value again"
                onClick={() => editor.dispatch({ type: "set-scene-scope", sceneId: editor.editingScene!.id, blockId: block.id, scope: "shared", value: block.enabled })}>Share</button>
            </>
          )}
          <Button className="pow-big" aria-pressed={block.enabled} onClick={() => editor.editBlockEnabled(block.id, !block.enabled)}><Power size={16} />{block.enabled ? "Block on" : "Block off"}</Button>
          <IconButton label="Duplicate" onClick={() => editor.dispatch({ type: "duplicate-block", blockId: block.id })}><Copy size={16} /></IconButton>
          <IconButton label="Reset to defaults" onClick={() => editor.dispatch({ type: "reset-block", blockId: block.id })}><RotateCcw size={16} /></IconButton>
          <Button variant="danger" onClick={() => { editor.dispatch({ type: "remove-block", blockId: block.id }); onClose(); }}><Trash2 size={16} />Delete</Button>
          <IconButton label="Close" onClick={onClose}><X size={16} /></IconButton>
        </span>
      </div>
      {issues.length > 0 && <ul className="drawer__issues">{issues.map((issue, i) => <li key={`${issue.code}-${i}`} className={`is-${issue.severity}`}>{issue.message}</li>)}</ul>}
      {modes.length > 1 && <div className="drawer__modes"><ChoiceStrip label="Type" family={family} value={definition!.id}
        options={modes.map((m) => ({ value: m.id, label: m.name }))} onChange={(id) => editor.dispatch({ type: "change-definition", blockId: block.id, definitionId: id })} /></div>}
      {editor.present.version === 4 && SCENE_BYPASS_TYPES.includes(block.type) && <div className="drawer__modes">
        <ChoiceStrip label="On scene bypass" family={family} value={block.sceneBypass ?? "letRing"}
          options={[{ value: "cut", label: "Cut" }, { value: "letRing", label: "Let ring" }]}
          onChange={(policy) => editor.dispatch({ type: "set-scene-bypass", blockId: block.id, policy })} /></div>}
      <div className="ctlgrid">
        {!definition && <p className="lb-note">This block type is unknown to this manager. You can turn it off or delete it.</p>}
        {isDual ? <>
          {general.map(control)}
          <h3 className="ctlgrid__lane">Lane A</h3>{left.map(control)}
          <h3 className="ctlgrid__lane">Lane B</h3>{right.map(control)}
        </> : controls.map(control)}
      </div>
    </section>
  );
}
