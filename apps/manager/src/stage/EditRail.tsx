import { ChevronLeft, ChevronRight, Footprints, Grid2x2, Plus, Redo2, RotateCcw, Save, SlidersHorizontal, Undo2 } from "lucide-react";
import { useRef } from "react";

import { Button, IconButton } from "../components/ui";
import { displayValue } from "../effects/display";
import type { NumberControl } from "../effects/types";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { findPresetBlockInPreset } from "../presets/editor/editorReducer";
import { applySceneToBlock } from "../presets/scenes/sceneView";
import { LiveState } from "./LiveState";

const FINE_BURST_MS = 900;
let fineSeq = 0;

type Props = {
  drawer: "none" | "block" | "global" | "scenes";
  focused?: { blockId: string; control: NumberControl };
  onOpen(drawer: "global" | "scenes"): void;
  onAdd(): void;
  onDone(): void;
};

export function EditRail({ drawer, focused, onOpen, onAdd, onDone }: Props) {
  const editor = usePresetEditorContext();
  const fineGesture = useRef<{ key: string; id: string; at: number } | null>(null);
  const block = focused ? findPresetBlockInPreset(editor.present, focused.blockId) : undefined;
  const shown = block ? applySceneToBlock(block, editor.editingScene) : undefined;
  const raw = focused && shown ? shown.params[focused.control.key] : undefined;
  const value = focused && typeof raw === "number" ? raw : focused?.control.defaultValue ?? 0;

  const fine = (direction: 1 | -1) => {
    if (!focused) return;
    const { control, blockId } = focused;
    const key = `${blockId}:${control.key}`;
    const now = Date.now();
    const previous = fineGesture.current;
    const id = previous && previous.key === key && now - previous.at <= FINE_BURST_MS ? previous.id : `fine-${++fineSeq}`;
    fineGesture.current = { key, id, at: now };
    const next = Math.min(control.maximum, Math.max(control.minimum, value + direction * control.step * 0.1));
    editor.editParameter(blockId, control.key, next, id);
  };
  const canExpress = focused !== undefined
    && editor.expressionTargets.some((t) => t.block.id === focused.blockId && t.parameters.some((p) => p.key === focused.control.key));
  const saveBlocked = !editor.validation.canSave;
  const saveReason = saveBlocked ? editor.validation.issues.find((i) => i.severity === "error")?.message : undefined;

  return (
    <nav className="rail" aria-label="Edit actions">
      <IconButton label="Undo" disabled={editor.editor.history.past.length === 0} onClick={() => editor.dispatch({ type: "undo" })}><Undo2 size={17} /></IconButton>
      <IconButton label="Redo" disabled={editor.editor.history.future.length === 0} onClick={() => editor.dispatch({ type: "redo" })}><Redo2 size={17} /></IconButton>
      <Button variant={editor.dirty ? "primary" : "secondary"} disabled={!editor.dirty || saveBlocked || editor.saving}
        title={saveReason ?? "Save to the slot"} onClick={() => void editor.save()}><Save size={16} /><span className="lbl">Save</span></Button>
      <Button variant="secondary" onClick={onAdd}><Plus size={16} /><span className="lbl">Add block</span></Button>
      <Button variant="secondary" aria-pressed={drawer === "global"} onClick={() => onOpen("global")}><SlidersHorizontal size={16} /><span className="lbl">Global</span></Button>
      <Button variant="secondary" aria-pressed={drawer === "scenes"} onClick={() => onOpen("scenes")}><Grid2x2 size={16} /><span className="lbl">{editor.present.sceneSet ? "Scenes" : "Create scenes"}</span></Button>
      {focused && (
        <div className="ctx">
          <span><span className="ctx__label">{focused.control.label}</span><span className="ctx__value">{displayValue(focused.control, value)}</span></span>
          <IconButton label="Fine decrease" className="hide-sm" onClick={() => fine(-1)}><ChevronLeft size={16} /></IconButton>
          <IconButton label="Fine increase" className="hide-sm" onClick={() => fine(1)}><ChevronRight size={16} /></IconButton>
          {canExpress && (
            <Button variant="secondary" className="hide-sm" onClick={() => editor.dispatch({
              type: "set-expression",
              expression: { blockId: focused.blockId, parameter: focused.control.key, minimum: focused.control.minimum, maximum: focused.control.maximum, inverted: false },
            })}><Footprints size={15} />Assign EXP</Button>
          )}
          <Button variant="secondary" className="hide-sm" onClick={() => editor.editParameter(focused.blockId, focused.control.key, focused.control.defaultValue)}><RotateCcw size={15} />Reset</Button>
        </div>
      )}
      <span className="rail__push" />
      <LiveState />
      {drawer !== "none" && <Button variant="primary" className="rail__done" onClick={onDone}>Done</Button>}
    </nav>
  );
}
