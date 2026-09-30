import { CloudOff, GripVertical } from "lucide-react";
import { useEffect, useMemo, useRef, useState } from "react";

import type { AssetKind, PresetBlock } from "../api/types";
import { missingPaths } from "../assets/assetRefs";
import { Button } from "../components/ui";
import { useDeviceSession } from "../connection/deviceSession";
import { findEffectDefinition } from "../effects/catalog";
import type { EffectDefinition, NumberControl } from "../effects/types";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import type { EditorAction } from "../presets/editor/editorTypes";
import { findPresetBlockInPreset } from "../presets/editor/editorReducer";
import { issuesForBlock } from "../presets/editor/presetValidation";
import type { AddTarget, PresetEditor } from "../presets/editor/usePresetEditor";
import { applySceneToBlock, sceneOwns } from "../presets/scenes/sceneView";
import { UnsavedChangesDialog } from "../presets/workspace/UnsavedChangesDialog";
import { BankBar } from "./BankBar";
import { BlockDrawer } from "./BlockDrawer";
import { ChainStage } from "./ChainStage";
import { ChipStrip } from "./ChipStrip";
import { EditRail } from "./EditRail";
import { GlobalDrawer } from "./GlobalDrawer";
import { ModuleDrawer } from "./ModuleDrawer";
import { ScenesDrawer } from "./ScenesDrawer";
import { addTargetAfter, shortcutFor, type Shortcut } from "./shortcuts";
import { StageHead, StageNotices } from "./StageHead";
import { withViewTransition } from "./viewTransition";
import { whereText } from "./whereText";
import "./workspace.css";

type Drawer = "none" | "block" | "global" | "scenes";

/** Block limits as before the redesign; Dual Rig lanes cap themselves at 10 inside ChainStage. */
const MAX_SERIAL_BLOCKS = 10;
const MAX_WDW_BLOCKS = 20;

function addAction(target: AddTarget, definitionId: string): EditorAction {
  if (target.kind === "lane") return { type: "add-lane-block", definitionId, rigId: target.rigId, lane: target.lane, index: target.index };
  if (target.kind === "wdw") return { type: "add-wdw-block", definitionId, lane: target.lane, index: target.index };
  return { type: "add-block", definitionId, index: target.index };
}

function focusedControl(block: PresetBlock | undefined, key: string | undefined) {
  if (!block || !key) return undefined;
  const control = findEffectDefinition(block)?.controls.find((c): c is NumberControl => c.kind === "number" && c.key === key);
  return control ? { blockId: block.id, control } : undefined;
}

function OfflineCard({ onConnection }: { onConnection(): void }) {
  return (
    <main className="edit edit--offline">
      <div className="offline">
        <CloudOff size={38} aria-hidden="true" />
        <p className="offline__eyebrow">Ardor Manager</p>
        <h1>Connect to your pedal</h1>
        <p>Manage preset chains, models, cabinet IRs and reverb IRs from one desktop workspace.</p>
        <Button variant="primary" onClick={onConnection}>Connect to device</Button>
      </div>
    </main>
  );
}

function Hints() {
  return (
    <p className="stage-hints">
      <span><GripVertical size={15} aria-hidden="true" />Drag a cap to move a block</span>
      <span><b className="lb-kbd">⌥</b><b className="lb-kbd">←</b><b className="lb-kbd">→</b>move the focused block</span>
      <span><b className="lb-kbd">B</b>bypass</span>
      <span><b className="lb-kbd">A</b>add after</span>
    </p>
  );
}

/** The selected block, else the block card with keyboard focus, as the edit scope shows it. */
function shortcutTarget(editor: PresetEditor): PresetBlock | undefined {
  const focused = (document.activeElement?.closest("[data-block-id]") as HTMLElement | null)?.dataset.blockId;
  const block = findPresetBlockInPreset(editor.present, editor.editor.selectedBlockId ?? focused);
  return block ? applySceneToBlock(block, editor.editingScene) : undefined;
}

/** Runs one shortcut against the editor. Returns false when the key should keep its default. */
function runShortcut(shortcut: Shortcut, editor: PresetEditor, drawerOpen: boolean, close: () => void): boolean {
  const selected = shortcutTarget(editor);
  switch (shortcut) {
    case "undo":
    case "redo":
      editor.dispatch({ type: shortcut });
      return true;
    case "save":
      if (editor.dirty && editor.validation.canSave && !editor.saving) void editor.save();
      return true;
    case "toggle":
      if (selected) editor.editBlockEnabled(selected.id, !selected.enabled);
      return Boolean(selected);
    case "remove":
      if (!selected) return false;
      editor.dispatch({ type: "remove-block", blockId: selected.id });
      close();
      return true;
    case "add":
      editor.setAddTarget(addTargetAfter(editor.present, selected?.id));
      return true;
    case "close":
      if (drawerOpen) close();
      return drawerOpen;
  }
}

export function StageWorkspace({ onManageFiles, onConnection }: { onManageFiles(kind: AssetKind): void; onConnection(): void }) {
  const editor = usePresetEditorContext();
  const session = useDeviceSession();
  const { present, dispatch } = editor;
  const { location, selectedBlockId } = editor.editor;
  const [bank, setBank] = useState(location.bank);
  const [drawer, setDrawer] = useState<Drawer>("none");
  const [focusedKey, setFocusedKey] = useState<string>();
  const connected = session.status === "connected" && Boolean(session.current);
  const shown: Drawer = drawer === "block" && !editor.inspectorBlock ? "none" : drawer;

  useEffect(() => setBank(location.bank), [location.bank, location.slot]);
  useEffect(() => setFocusedKey(undefined), [selectedBlockId]);

  const close = () => withViewTransition(() => {
    setDrawer("none");
    dispatch({ type: "select-block" });
  });
  const select = (blockId: string) => withViewTransition(() => {
    dispatch({ type: "select-block", blockId });
    setDrawer("block");
  });
  const open = (next: "global" | "scenes") => withViewTransition(() => {
    dispatch({ type: "select-block" });
    setDrawer(next);
  });
  const add = (definition: EffectDefinition) => {
    dispatch(addAction(editor.addTarget ?? addTargetAfter(present), definition.id));
    editor.setAddTarget(undefined);
  };

  const onKey = useRef<(event: KeyboardEvent) => void>(() => undefined);
  onKey.current = (event) => {
    const shortcut = connected ? shortcutFor(event) : undefined;
    if (!shortcut) return;
    const dialogOpen = Boolean(editor.addTarget || editor.pendingLocation);
    if (shortcut === "save") {
      event.preventDefault();
      if (!dialogOpen) runShortcut(shortcut, editor, shown !== "none", close);
      return;
    }
    if (!dialogOpen && runShortcut(shortcut, editor, shown !== "none", close)) event.preventDefault();
  };
  useEffect(() => {
    const listener = (event: KeyboardEvent) => onKey.current(event);
    window.addEventListener("keydown", listener);
    return () => window.removeEventListener("keydown", listener);
  }, []);

  const missing = useMemo(
    () => missingPaths(present, { models: session.models, irs: session.irs, reverbIrs: session.reverbIrs }),
    [present, session.models, session.irs, session.reverbIrs],
  );
  const focused = shown === "block" ? focusedControl(editor.inspectorBlock, focusedKey) : undefined;

  if (!connected) return <OfflineCard onConnection={onConnection} />;

  const active = session.device?.active;
  const maxed = present.routing === "wdw" ? editor.allBlocks.length >= MAX_WDW_BLOCKS : present.blocks.length >= MAX_SERIAL_BLOCKS;
  const addAtEnd = () => editor.setAddTarget(addTargetAfter(present));

  return (
    <div className="edit">
      <BankBar bank={bank} editing={location} live={active ? { bank: active.bank, slot: active.slot } : undefined}
        dirty={editor.dirty} draft={present} disabled={editor.saving || session.busy.apply}
        onBank={setBank} onOpen={editor.selectLocation} />
      <main className="edit__main">
        {shown === "none" ? (
          <>
            <StageHead />
            <StageNotices />
            <div className="edit__stage">
              <ChainStage blocks={editor.displayedBlocks} wdw={present.routing === "wdw" ? editor.displayedWdw : undefined}
                selectedId={selectedBlockId} issuesFor={(id) => issuesForBlock(editor.validation, id)}
                missingFile={(block) => Boolean(block.asset) && missing.has(block.asset)}
                sceneOwnsEnabled={(id) => sceneOwns(editor.editingScene, id)} maxed={maxed}
                onSelect={select} onToggle={(block) => editor.editBlockEnabled(block.id, !block.enabled)}
                onAdd={editor.setAddTarget} onMove={dispatch} />
            </div>
            <Hints />
          </>
        ) : (
          <>
            <div className="edit__chips">
              <ChipStrip blocks={editor.displayedBlocks} wdw={present.routing === "wdw" ? editor.displayedWdw : undefined} selectedId={selectedBlockId} onSelect={select} onMove={dispatch} onAdd={addAtEnd} />
            </div>
            <StageNotices />
            {shown === "block" && editor.inspectorBlock && (
              <BlockDrawer block={editor.inspectorBlock} issues={issuesForBlock(editor.validation, editor.inspectorBlock.id)}
                focusedKey={focusedKey} onFocusKey={setFocusedKey} onClose={close} onManageFiles={onManageFiles} />
            )}
            {shown === "global" && <GlobalDrawer onClose={close} />}
            {shown === "scenes" && <ScenesDrawer onClose={close} />}
          </>
        )}
      </main>
      <EditRail drawer={shown} focused={focused} onOpen={open} onAdd={() => editor.setAddTarget(addTargetAfter(present, selectedBlockId))} onDone={close} />
      <ModuleDrawer open={Boolean(editor.addTarget)} where={whereText(editor.addTarget, present)} disabledIds={editor.disabledDefinitions}
        onOpenChange={(isOpen) => { if (!isOpen) editor.setAddTarget(undefined); }} onChoose={add} />
      <UnsavedChangesDialog open={Boolean(editor.pendingLocation)} busy={editor.saving} onChoice={(choice) => void editor.resolveNavigation(choice)} />
    </div>
  );
}
