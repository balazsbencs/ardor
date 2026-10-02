import { usePresetEditorContext } from "../presets/editor/EditorContext";

/** Preset or one scene as the edit scope, like the pedal's scene screen. */
export function SceneScope() {
  const editor = usePresetEditorContext();
  const scenes = editor.present.sceneSet?.scenes;
  if (!scenes) return null;
  const liveId = editor.liveSceneId;
  return (
    <div className="seg" role="group" aria-label="Edit scope">
      <button type="button" className="btn btn--sm" aria-pressed={!editor.editingScene} onClick={() => editor.dispatch({ type: "clear-scene" })}>Preset</button>
      {scenes.map((scene, index) => (
        <button key={scene.id} type="button" className="btn btn--sm" aria-pressed={editor.editingScene?.id === scene.id}
          onClick={() => editor.dispatch({ type: "select-scene", sceneId: scene.id })}>
          {index + 1} {scene.name}{liveId === scene.id && <i className="lampdot" title="Live scene" />}
        </button>
      ))}
    </div>
  );
}
