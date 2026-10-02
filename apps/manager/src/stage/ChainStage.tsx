import {
  DndContext, KeyboardSensor, MouseSensor, TouchSensor, closestCenter, useDroppable, useSensor, useSensors, type DragEndEvent,
} from "@dnd-kit/core";
import { SortableContext, horizontalListSortingStrategy, sortableKeyboardCoordinates, useSortable } from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";
import { Plus, Split } from "lucide-react";

import type { PresetBlock, WdwRouting } from "../api/types";
import type { AddTarget } from "../presets/editor/usePresetEditor";
import type { EditorAction } from "../presets/editor/editorTypes";
import type { ValidationIssue } from "../presets/editor/presetValidation";
import { BlockCard } from "./BlockCard";
import { CHAIN_FULL, LANE_FULL, MAX_LANE_BLOCKS, WDW_FULL } from "./chainLimits";
import { resolveDrop, type ListId } from "./dropTarget";
import "./stage.css";

type Props = {
  blocks: PresetBlock[];
  wdw?: WdwRouting;
  selectedId?: string;
  issuesFor(id: string): ValidationIssue[];
  missingFile(block: PresetBlock): boolean;
  sceneOwnsEnabled(id: string): boolean;
  maxed: boolean;
  onSelect(id: string): void;
  onToggle(block: PresetBlock): void;
  onAdd(target: AddTarget): void;
  onMove(action: EditorAction): void;
};

type ItemData = { listId: ListId; index: number };

function targetFor(listId: ListId, index: number): AddTarget {
  if (listId === "top") return { kind: "top", index };
  if (listId === "wdw:dry" || listId === "wdw:wet") return { kind: "wdw", lane: listId === "wdw:dry" ? "dry" : "wet", index };
  const [, rigId, lane] = listId.split(":");
  return { kind: "lane", rigId, lane: lane as "left" | "right", index };
}

function Insert({ listId, index, blocked, label, onAdd }: { listId: ListId; index: number; blocked?: string; label: string; onAdd(target: AddTarget): void }) {
  return <button type="button" className="ins" disabled={blocked !== undefined} aria-label={label} title={blocked ?? "Add a block"}
    onClick={() => onAdd(targetFor(listId, index))}><Plus size={14} strokeWidth={2.4} /></button>;
}

function SortableCard({ block, listId, index, count, laneTag, props }: { block: PresetBlock; listId: ListId; index: number; count: number; laneTag?: "A" | "B" | "DRY" | "WET"; props: Props }) {
  const sortable = useSortable({ id: block.id, data: { listId, index } satisfies ItemData });
  const nudge = (direction: -1 | 1) => {
    const to = Math.min(count - 1, Math.max(0, index + direction));
    const action = resolveDrop({ listId, index }, { listId, index: to }, block.id);
    if (action) props.onMove(action);
  };
  return <BlockCard block={block} selected={props.selectedId === block.id} issues={props.issuesFor(block.id)}
    missingFile={props.missingFile(block)} sceneOwnsEnabled={props.sceneOwnsEnabled(block.id)} laneTag={laneTag}
    onSelect={() => props.onSelect(block.id)} onToggle={() => props.onToggle(block)} onNudge={nudge}
    innerRef={sortable.setNodeRef} style={{ transform: CSS.Transform.toString(sortable.transform), transition: sortable.transition }}
    handleProps={{ ...sortable.attributes, ...sortable.listeners }} dragging={sortable.isDragging} />;
}

function List({ listId, blocks, props, laneTag, emptyLabel }: { listId: ListId; blocks: PresetBlock[]; props: Props; laneTag?: "A" | "B" | "DRY" | "WET"; emptyLabel?: string }) {
  const { setNodeRef } = useDroppable({ id: `list:${listId}`, data: { listId, index: blocks.length } satisfies ItemData, disabled: blocks.length > 0 });
  const insertLabel = (index: number) => (listId === "top" ? `Add a block at position ${index + 1}` : `Add a block to lane ${laneTag}${blocks.length ? ` at position ${index + 1}` : ""}`);
  // Dual Rig and WDW lanes hold 10 blocks each; the reducer drops an add past that.
  const laneFull = listId !== "top" && blocks.length >= MAX_LANE_BLOCKS;
  const blocked = laneFull ? LANE_FULL : props.maxed ? (props.wdw ? WDW_FULL : CHAIN_FULL) : undefined;
  return (
    <div ref={setNodeRef} className="chain__list">
      <SortableContext items={blocks.map(({ id }) => id)} strategy={horizontalListSortingStrategy}>
        {blocks.map((block, index) => <span key={block.id} className="chain__item">
          <Insert listId={listId} index={index} blocked={blocked} label={insertLabel(index)} onAdd={props.onAdd} />
          {block.lanes ? <Rig rig={block} props={props} /> : <SortableCard block={block} listId={listId} index={index} count={blocks.length} laneTag={laneTag} props={props} />}
        </span>)}
      </SortableContext>
      <Insert listId={listId} index={blocks.length} blocked={blocked} label={blocks.length ? insertLabel(blocks.length) : `Add a block to lane ${laneTag}`} onAdd={props.onAdd} />
      {!blocks.length && emptyLabel && <span className="chain__empty">{emptyLabel}</span>}
    </div>
  );
}

function Rig({ rig, props }: { rig: PresetBlock; props: Props }) {
  const lanes = rig.lanes!;
  return (
    <div className="rig">
      <button type="button" className="rig__node" onClick={() => props.onSelect(rig.id)} aria-label="Dual Rig split. Open its settings."><Split size={18} /><b>SPLIT</b>Dual Rig</button>
      <div className="rig__lanes">
        <div className="rig__lane"><span className="rig__tag rig__tag--a">A</span><List listId={`lane:${rig.id}:left`} blocks={lanes.left.blocks} props={props} laneTag="A" emptyLabel="Empty lane. Drop a block here." /></div>
        <div className="rig__lane"><span className="rig__tag rig__tag--b">B</span><List listId={`lane:${rig.id}:right`} blocks={lanes.right.blocks} props={props} laneTag="B" emptyLabel="Empty lane. Drop a block here." /></div>
      </div>
      <div className="rig__node"><b>JOIN</b>L / R</div>
    </div>
  );
}

export function ChainStage(props: Props) {
  const sensors = useSensors(
    useSensor(MouseSensor, { activationConstraint: { distance: 6 } }),
    useSensor(TouchSensor, { activationConstraint: { delay: 250, tolerance: 8 } }),
    useSensor(KeyboardSensor, { coordinateGetter: sortableKeyboardCoordinates }),
  );
  const onDragEnd = ({ active, over }: DragEndEvent) => {
    const from = active.data.current as ItemData | undefined;
    const to = over?.data.current as ItemData | undefined;
    if (!from || !to) return;
    const action = resolveDrop(from, to, String(active.id));
    if (action) props.onMove(action);
  };
  return (
    <section className="chain-stage" aria-label="Signal chain">
      <DndContext sensors={sensors} collisionDetection={closestCenter} onDragEnd={onDragEnd}>
        <div className="chain">
          <div className="jack">IN<small>MONO</small></div><span className="chain__wire" />
          {props.wdw ? (
            <div className="rig">
              <div className="rig__node"><b>SPLIT</b>WDW</div>
              <div className="rig__lanes">
                <div className="rig__lane"><span className="rig__tag rig__tag--a">DRY</span><List listId="wdw:dry" blocks={props.wdw.dry.blocks} props={props} laneTag="DRY" emptyLabel="Empty lane." /></div>
                <div className="rig__lane"><span className="rig__tag rig__tag--b">WET</span><List listId="wdw:wet" blocks={props.wdw.wet.blocks} props={props} laneTag="WET" emptyLabel="Empty lane." /></div>
              </div>
              <div className="rig__node"><b>JOIN</b>L / R</div>
            </div>
          ) : <List listId="top" blocks={props.blocks} props={props} />}
          <span className="chain__wire" /><div className="jack">OUT<small>STEREO</small></div>
        </div>
      </DndContext>
      {!props.wdw && props.blocks.length === 0 && <p className="chain__hint">Start with an amp. Press + to add a block.</p>}
    </section>
  );
}
