import { X } from "lucide-react";

import { Button, IconButton } from "../components/ui";
import { useDeviceSession } from "../connection/deviceSession";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { SceneWorkspaceBar } from "../presets/scenes/SceneWorkspaceBar";
import "./drawer.css";

export function ScenesDrawer({ onClose }: { onClose(): void }) {
  const editor = usePresetEditorContext();
  const session = useDeviceSession();
  const { present, dispatch, dirty, runtimeMatchesDraft } = editor;
  const recallHint = dirty || !runtimeMatchesDraft
    ? "Apply this version to recall it."
    : "Scene recall is unavailable on this connection.";
  const scenesUnsupported = session.device?.supportedPresetVersion !== undefined && session.device.supportedPresetVersion < 4;
  return (
    <section className="drawer fam-mod" aria-label="Scenes">
      <div className="drawer__head">
        <h2>Scenes</h2>
        <span className="drawer__actions"><IconButton label="Close" onClick={onClose}><X size={16} /></IconButton></span>
      </div>
      {present.sceneSet ? (
        <SceneWorkspaceBar
          sceneSet={present.sceneSet}
          editingSceneId={editor.editor.editingSceneId ?? present.sceneSet.defaultSceneId}
          liveSceneId={editor.liveSceneId}
          recallDisabled={!runtimeMatchesDraft || !session.device?.capabilities.sceneRecall}
          recallHint={recallHint}
          recalling={editor.recallingScene}
          onSelect={(sceneId) => dispatch({ type: "select-scene", sceneId })}
          onRecall={() => void editor.recallScene()}
          onName={(sceneId, name) => dispatch({ type: "set-scene-name", sceneId, name })}
          onEnterTime={(sceneId, value) => dispatch({ type: "set-scene-enter-time", sceneId, value })}
          onTrim={(sceneId, value) => dispatch({ type: "set-scene-trim", sceneId, value })}
          onDefault={(sceneId) => dispatch({ type: "set-default-scene", sceneId })}
          onOpenIn={(value) => dispatch({ type: "set-scene-open-in", value })}
          onCopy={(sourceSceneId, destinationSceneId) => dispatch({ type: "copy-scene", sourceSceneId, destinationSceneId })}
          onSwap={(firstSceneId, secondSceneId) => dispatch({ type: "swap-scenes", firstSceneId, secondSceneId })}
          presentTarget={editor.presentSceneTarget}
          sharedRows={editor.sharedComparisonRows}
          onCopyRow={(rowKey, sourceSceneId) => dispatch({ type: "copy-scene-row-across", rowKey, sourceSceneId })}
        />
      ) : (
        <div className="scene-note">
          <span>Scenes change blocks and values inside one preset. FS 1 to 4 pick them on the pedal.</span>
          <Button variant="primary" disabled={editor.saving || scenesUnsupported} onClick={() => dispatch({ type: "enable-scenes" })}>Create four scenes</Button>
        </div>
      )}
    </section>
  );
}
