import { AlertCircle, Check, CloudOff, Redo2, Save, Send, SlidersHorizontal, Undo2 } from "lucide-react";

import { Button, IconButton, StatusBadge } from "../../components/ui";
import { useDeviceSession } from "../../connection/deviceSession";
import { PresetSidebar } from "../browser/PresetSidebar";
import { BlockBrowser } from "../block-browser/BlockBrowser";
import { ChainCanvas } from "../chain/ChainCanvas";
import { usePresetEditorContext } from "../editor/EditorContext";
import { issuesForBlock } from "../editor/presetValidation";
import { BlockInspector } from "../inspector/BlockInspector";
import { UnsavedChangesDialog } from "./UnsavedChangesDialog";
import { WdwRoutingCanvas } from "../chain/WdwRoutingCanvas";
import { SceneWorkspaceBar } from "../scenes/SceneWorkspaceBar";

export function PresetWorkspace({ onAssets, onConnection }: { onAssets(): void; onConnection(): void }) {
  const session = useDeviceSession();
  const {
    editor, dispatch, present, dirty, validation, allBlocks, editingScene, displayedBlocks, displayedWdw,
    inspectorBlock, displayedInputGain, sceneInputOwned, sceneScopeFor, editBlockEnabled, editParameter,
    editWdwMix, expressionTargets, expressionTarget, expressionParameter, enableExpression, patchExpression,
    addTarget, setAddTarget, disabledDefinitions, selectLocation, pendingLocation, resolveNavigation, save,
    apply, saveAndApply, recallScene, saving, recallingScene, actionError, applied, runtimeMatchesDraft,
    sharedComparisonRows, presentSceneTarget,
  } = usePresetEditorContext();

  if (session.status !== "connected" || !session.current) {
    return <main className="workspace workspace--offline"><div className="offline-card"><CloudOff size={38} /><p className="eyebrow">Ardor Manager</p><h1>Connect to your pedal</h1><p>Manage preset chains, models, cabinet IRs and reverb IRs from one desktop workspace.</p><Button variant="primary" onClick={onConnection}>Connect to device</Button></div></main>;
  }

  const locationLabel = `Bank ${String(editor.location.bank).padStart(3, "0")} / Slot ${editor.location.slot + 1}`;
  const applyBlocked = !validation.canApply || dirty || session.busy.apply || saving;
  const recallHint = dirty || !runtimeMatchesDraft
    ? "Apply this version to recall it."
    : "Scene recall is unavailable on this connection.";
  return (
    <main className="workspace">
      <PresetSidebar summaries={session.presets} selected={editor.location} active={session.device?.active} disabled={saving || session.busy.apply} onSelect={selectLocation} />
      <section className="workspace-main">
        <header className="preset-header">
          <div><p className="eyebrow">{locationLabel}</p><input aria-label="Preset name" className="preset-name-input" value={present.name} onChange={(event) => dispatch({ type: "set-name", name: event.target.value })} /><div className="preset-header__meta">{dirty && <StatusBadge tone="warning">Unsaved changes</StatusBadge>}{applied?.bank === editor.location.bank && applied.slot === editor.location.slot && <StatusBadge tone="success"><Check size={13} /> Applied this session</StatusBadge>}</div></div>
          <div className="preset-actions"><IconButton label="Undo" disabled={editor.history.past.length === 0} onClick={() => dispatch({ type: "undo" })}><Undo2 size={17} /></IconButton><IconButton label="Redo" disabled={editor.history.future.length === 0} onClick={() => dispatch({ type: "redo" })}><Redo2 size={17} /></IconButton>{!present.sceneSet && <Button variant="secondary" disabled={saving || (session.device?.supportedPresetVersion !== undefined && session.device.supportedPresetVersion < 4)} onClick={() => dispatch({ type: "enable-scenes" })}>Enable scenes</Button>}<Button variant="secondary" disabled={!dirty || !validation.canSave || saving} onClick={() => void save()}><Save size={16} /> {saving ? "Saving…" : "Save"}</Button><Button variant="primary" disabled={!validation.canApply || saving || session.busy.apply} onClick={() => void saveAndApply()}><Send size={16} /> Save & Apply</Button><Button variant="quiet" disabled={applyBlocked} onClick={() => void apply()}>Apply</Button></div>
        </header>
        {present.sceneSet && <SceneWorkspaceBar
          sceneSet={present.sceneSet}
          editingSceneId={editor.editingSceneId ?? present.sceneSet.defaultSceneId}
          liveSceneId={session.device?.active?.liveSceneId}
          recallDisabled={!runtimeMatchesDraft || !session.device?.capabilities.sceneRecall}
          recallHint={recallHint}
          recalling={recallingScene}
          onSelect={(sceneId) => dispatch({ type: "select-scene", sceneId })}
          onRecall={() => void recallScene()}
          onName={(sceneId, name) => dispatch({ type: "set-scene-name", sceneId, name })}
          onEnterTime={(sceneId, value) => dispatch({ type: "set-scene-enter-time", sceneId, value })}
          onTrim={(sceneId, value) => dispatch({ type: "set-scene-trim", sceneId, value })}
          onDefault={(sceneId) => dispatch({ type: "set-default-scene", sceneId })}
          onOpenIn={(value) => dispatch({ type: "set-scene-open-in", value })}
          onCopy={(sourceSceneId, destinationSceneId) => dispatch({ type: "copy-scene", sourceSceneId, destinationSceneId })}
          onSwap={(firstSceneId, secondSceneId) => dispatch({ type: "swap-scenes", firstSceneId, secondSceneId })}
          presentTarget={presentSceneTarget}
          sharedRows={sharedComparisonRows}
          onCopyRow={(rowKey, sourceSceneId) => dispatch({ type: "copy-scene-row-across", rowKey, sourceSceneId })}
        />}
        <div className="global-strip"><SlidersHorizontal size={17} /><label>Input<input aria-label="Input gain" type="number" min={-60} max={24} value={displayedInputGain} onChange={(event) => {
          const value = Number(event.target.value);
          if (sceneInputOwned && editingScene) dispatch({ type: "set-scene-input-gain", sceneId: editingScene.id, value });
          else dispatch({ type: "set-global", key: "inputGainDb", value });
        }} /><small>dB</small></label>{editingScene && <label>Input scope<select aria-label="Input gain scope" value={sceneInputOwned ? "scene" : "shared"} onChange={(event) => dispatch({ type: "set-scene-input-scope", sceneId: editingScene.id, scope: event.target.value as "shared" | "scene" })}><option value="shared">Shared</option><option value="scene">This scene</option></select></label>}<label>Output<input type="number" min={-60} max={24} value={present.global.outputGainDb} onChange={(event) => dispatch({ type: "set-global", key: "outputGainDb", value: Number(event.target.value) })} /><small>dB</small></label><label className="routing-picker">Topology<select aria-label="Preset topology" value={present.routing} onChange={(event) => dispatch({ type: "set-routing", routing: event.target.value as "serial" | "wdw" })}><option value="serial">Serial</option><option value="wdw">Wet / dry / wet</option></select></label><span>{present.routing === "wdw" ? "Two complete lanes · bounded pair execution" : "Serial routing"}</span></div>
        <div className="expression-strip">
          <label className="expression-strip__enable">
            <input
              type="checkbox"
              checked={present.expression !== undefined}
              disabled={expressionTargets.length === 0}
              onChange={(event) => {
                if (event.target.checked) enableExpression();
                else dispatch({ type: "set-expression" });
              }}
            />
            Expression pedal
          </label>
          {present.expression ? <>
            <label>Effect
              <select
                aria-label="Expression effect"
                value={present.expression.blockId}
                onChange={(event) => {
                  const target = expressionTargets.find(({ block }) => block.id === event.target.value);
                  const parameter = target?.parameters[0];
                  if (!target || !parameter) return;
                  dispatch({
                    type: "set-expression",
                    expression: {
                      blockId: target.block.id,
                      parameter: parameter.key,
                      minimum: parameter.minimum,
                      maximum: parameter.maximum,
                      inverted: present.expression?.inverted ?? false,
                    },
                  });
                }}
              >
                {expressionTargets.map(({ block, name }) =>
                  <option key={block.id} value={block.id}>{name} · {block.id}</option>)}
              </select>
            </label>
            <label>Parameter
              <select
                aria-label="Expression parameter"
                value={present.expression.parameter}
                onChange={(event) => {
                  const parameter = expressionTarget?.parameters.find(({ key }) => key === event.target.value);
                  if (!parameter) return;
                  patchExpression({
                    parameter: parameter.key,
                    minimum: parameter.minimum,
                    maximum: parameter.maximum,
                  });
                }}
              >
                {expressionTarget?.parameters.map((parameter) =>
                  <option key={parameter.key} value={parameter.key}>{parameter.label}</option>)}
              </select>
            </label>
            <label>Minimum
              <input
                aria-label="Expression minimum"
                type="number"
                step={expressionParameter?.step ?? "any"}
                value={present.expression.minimum}
                onChange={(event) => patchExpression({ minimum: Number(event.target.value) })}
              />
            </label>
            <label>Maximum
              <input
                aria-label="Expression maximum"
                type="number"
                step={expressionParameter?.step ?? "any"}
                value={present.expression.maximum}
                onChange={(event) => patchExpression({ maximum: Number(event.target.value) })}
              />
            </label>
            <label>
              <input
                type="checkbox"
                checked={present.expression.inverted}
                onChange={(event) => patchExpression({ inverted: event.target.checked })}
              />
              Invert
            </label>
          </> : <small>
            {expressionTargets.length > 0
              ? "Enable to assign one effect parameter in this preset."
              : "Add a supported effect before assigning the pedal."}
          </small>}
        </div>
        {(!validation.canSave || validation.issues.length > 0 || actionError) && <div className="workspace-alert" role="alert"><AlertCircle size={17} /><div>{actionError ? <p>{actionError}</p> : <p>{validation.issues[0]?.message}</p>}<small>{!validation.canSave ? "Fix this before saving." : !validation.canApply ? "You can save this draft, but cannot apply it yet." : "Review the highlighted block."}</small></div></div>}
        {present.routing === "wdw" && displayedWdw
          ? <WdwRoutingCanvas routing={displayedWdw} selectedBlockId={editor.selectedBlockId} issuesFor={(id) => issuesForBlock(validation, id)} maxed={allBlocks.length >= 20} onSelect={(blockId) => dispatch({ type: "select-block", blockId })} onAdd={(lane, index) => setAddTarget({ kind: "wdw", lane, index })} onMove={(lane, blockId, index) => dispatch({ type: "move-wdw-block", lane, blockId, index })} onToggle={editBlockEnabled} onDuplicate={(blockId) => dispatch({ type: "duplicate-block", blockId })} onReset={(blockId) => dispatch({ type: "reset-block", blockId })} onDelete={(blockId) => dispatch({ type: "remove-block", blockId })} onMix={editWdwMix} />
          : <ChainCanvas blocks={displayedBlocks} selectedBlockId={editor.selectedBlockId} issuesFor={(id) => issuesForBlock(validation, id)} maxed={present.blocks.length >= 10} onSelect={(blockId) => dispatch({ type: "select-block", blockId })} onAdd={(index) => setAddTarget({ kind: "top", index })} onMove={(blockId, index) => dispatch({ type: "move-block", blockId, index })} onLaneAdd={(rigId, lane, index) => setAddTarget({ kind: "lane", rigId, lane, index })} onLaneMove={(rigId, blockId, lane, index) => dispatch({ type: "move-lane-block", rigId, blockId, lane, index })} onToggle={editBlockEnabled} onDuplicate={(blockId) => dispatch({ type: "duplicate-block", blockId })} onReset={(blockId) => dispatch({ type: "reset-block", blockId })} onDelete={(blockId) => dispatch({ type: "remove-block", blockId })} />}
      </section>
      <BlockInspector block={inspectorBlock} issues={inspectorBlock ? issuesForBlock(validation, inspectorBlock.id) : []} models={session.models} irs={session.irs} reverbIrs={session.reverbIrs} scenesEnabled={present.version === 4} onToggle={editBlockEnabled} onParam={editParameter} sceneScopeFor={sceneScopeFor} onSceneScope={(blockId, parameter, scope, value) => editingScene && dispatch({ type: "set-scene-scope", sceneId: editingScene.id, blockId, parameter, scope, value })} onSceneBypass={(blockId, policy) => dispatch({ type: "set-scene-bypass", blockId, policy })} onAsset={(blockId, asset) => dispatch({ type: "set-block-asset", blockId, asset })} onMode={(blockId, definitionId) => dispatch({ type: "change-definition", blockId, definitionId })} onEqBand={(blockId, band, patch) => dispatch({ type: "set-eq-band", blockId, band, patch })} onReset={(blockId) => dispatch({ type: "reset-block", blockId })} onDuplicate={(blockId) => dispatch({ type: "duplicate-block", blockId })} onDelete={(blockId) => dispatch({ type: "remove-block", blockId })} onAssets={onAssets} onClose={() => dispatch({ type: "select-block" })} />
      <BlockBrowser open={addTarget !== undefined} onOpenChange={(open) => { if (!open) setAddTarget(undefined); }} disabledIds={disabledDefinitions} onChoose={(definition) => {
        if (addTarget?.kind === "lane") {
          dispatch({ type: "add-lane-block", definitionId: definition.id, rigId: addTarget.rigId, lane: addTarget.lane, index: addTarget.index });
        } else if (addTarget?.kind === "wdw") {
          dispatch({ type: "add-wdw-block", definitionId: definition.id, lane: addTarget.lane, index: addTarget.index });
        } else {
          dispatch({ type: "add-block", definitionId: definition.id, index: addTarget?.index ?? present.blocks.length });
        }
        setAddTarget(undefined);
      }} />
      <UnsavedChangesDialog open={pendingLocation !== undefined} busy={saving} onChoice={(choice) => void resolveNavigation(choice)} />
    </main>
  );
}
