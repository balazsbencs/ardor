import { AlertCircle } from "lucide-react";

import { Button } from "../components/ui";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { Tag } from "../ui/Tag";
import { SceneScope } from "./SceneScope";

/** EDIT, the preset name, the block count and the scene scope, above the chain. */
export function StageHead() {
  const editor = usePresetEditorContext();
  const count = editor.allBlocks.length;
  return (
    <div className="stagehead">
      <h1>Edit</h1>
      <input className="stagehead__name" aria-label="Preset name" value={editor.present.name} spellCheck={false}
        onChange={(event) => editor.dispatch({ type: "set-name", name: event.target.value })} />
      <span className="stagehead__count">{count} {count === 1 ? "block" : "blocks"}</span>
      {editor.dirty && <span className="stagehead__dirty"><Tag tone="warn">MODIFIED</Tag></span>}
      <span className="stagehead__push" />
      <SceneScope />
    </div>
  );
}

/** The last save or load error, and preset-level issues with their fix. Block issues stay on the cards. */
export function StageNotices() {
  const editor = usePresetEditorContext();
  const issues = editor.validation.issues.filter(({ blockId }) => !blockId);
  if (!editor.actionError && issues.length === 0) return null;
  return (
    <div className="stage-notices">
      {editor.actionError && <p className="stage-notice is-error" role="alert"><AlertCircle size={16} />{editor.actionError}</p>}
      {issues.map((issue, index) => (
        <p key={`${issue.code}-${index}`} className={`stage-notice is-${issue.severity}`}>
          <AlertCircle size={16} /><span>{issue.message}</span>
          {issue.code === "scene-set-required" && (
            <Button variant="secondary" onClick={() => editor.dispatch({ type: "enable-scenes" })}>Create four scenes</Button>
          )}
        </p>
      ))}
    </div>
  );
}
