import {
  DndContext, KeyboardSensor, MouseSensor, TouchSensor, closestCenter, useDroppable, useSensor, useSensors, type DragEndEvent,
} from "@dnd-kit/core";
import { SortableContext, horizontalListSortingStrategy, sortableKeyboardCoordinates, useSortable } from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";
import { Plus, Split } from "lucide-react";
import type { KeyboardEvent } from "react";

import type { PresetBlock, WdwRouting } from "../api/types";
import { cx } from "../components/ui";
import type { EditorAction } from "../presets/editor/editorTypes";
import { capFor, familyOf } from "../ui/family";
import { blockTitle } from "./BlockCard";
import { resolveDrop, type ListId } from "./dropTarget";
import "./drawer.css";

type Props = {
  blocks: PresetBlock[];
  /** WDW lanes; when given they replace the top-level chain, as on the chain stage. */
  wdw?: WdwRouting;
  selectedId?: string;
  onSelect(id: string): void;
  onMove(action: EditorAction): void;
  onAdd(): void;
};

type ItemData = { listId: ListId; index: number };

function Chip({ block, listId, index, count, props }: { block: PresetBlock; listId: ListId; index: number; count: number; props: Props }) {
  const sortable = useSortable({ id: block.id, data: { listId, index } satisfies ItemData });
  const onKeyDown = (event: KeyboardEvent<HTMLButtonElement>) => {
    if (event.altKey && (event.key === "ArrowLeft" || event.key === "ArrowRight")) {
      event.preventDefault();
      const to = Math.min(count - 1, Math.max(0, index + (event.key === "ArrowRight" ? 1 : -1)));
      const action = resolveDrop({ listId, index }, { listId, index: to }, block.id);
      if (action) props.onMove(action);
      return;
    }
    sortable.listeners?.onKeyDown?.(event);
  };
  return (
    <button type="button" ref={sortable.setNodeRef} {...sortable.attributes} {...sortable.listeners} onKeyDown={onKeyDown}
      className={cx("chip", `fam-${familyOf(block.type)}`, props.selectedId === block.id && "is-sel", !block.enabled && "is-off", sortable.isDragging && "is-dragging")}
      aria-current={props.selectedId === block.id ? "true" : undefined}
      style={{ transform: CSS.Transform.toString(sortable.transform), transition: sortable.transition, viewTransitionName: `block-${block.id}` }}
      onClick={() => props.onSelect(block.id)}>
      <small>{capFor(block.type)}{block.enabled ? "" : " · off"}</small><b>{blockTitle(block)}</b>
    </button>
  );
}

function List({ listId, blocks, props, lane }: { listId: ListId; blocks: PresetBlock[]; props: Props; lane?: boolean }) {
  const { setNodeRef } = useDroppable({ id: `list:${listId}`, data: { listId, index: blocks.length } satisfies ItemData, disabled: blocks.length > 0 });
  return (
    <div ref={setNodeRef} className={cx("chips__list", lane && "chips__list--lane")}>
      <SortableContext items={blocks.map(({ id }) => id)} strategy={horizontalListSortingStrategy}>
        {blocks.map((block, index) => block.lanes
          ? <Rig key={block.id} rig={block} props={props} />
          : <Chip key={block.id} block={block} listId={listId} index={index} count={blocks.length} props={props} />)}
      </SortableContext>
    </div>
  );
}

function Rig({ rig, props }: { rig: PresetBlock; props: Props }) {
  const lanes = rig.lanes!;
  return (
    <div className="chip-rig">
      <button type="button" className="chip-rig__head" aria-current={props.selectedId === rig.id ? "true" : undefined}
        aria-label="Dual Rig split. Open its settings." onClick={() => props.onSelect(rig.id)}><Split size={16} />SPLIT</button>
      <div className="chip-rig__lanes">
        <div className="chip-rig__lane"><span className="chip-lane">A</span><List listId={`lane:${rig.id}:left`} blocks={lanes.left.blocks} props={props} lane /></div>
        <div className="chip-rig__lane"><span className="chip-lane">B</span><List listId={`lane:${rig.id}:right`} blocks={lanes.right.blocks} props={props} lane /></div>
      </div>
    </div>
  );
}

function Wdw({ wdw, props }: { wdw: WdwRouting; props: Props }) {
  return (
    <div className="chip-rig chip-rig--wdw">
      <div className="chip-rig__head" aria-hidden="true"><Split size={16} />WDW</div>
      <div className="chip-rig__lanes">
        <div className="chip-rig__lane"><span className="chip-lane chip-lane--word">DRY</span><List listId="wdw:dry" blocks={wdw.dry.blocks} props={props} lane /></div>
        <div className="chip-rig__lane"><span className="chip-lane chip-lane--word">WET</span><List listId="wdw:wet" blocks={wdw.wet.blocks} props={props} lane /></div>
      </div>
    </div>
  );
}

/** The chain folded into one row of chips while a block's drawer is open. */
export function ChipStrip(props: Props) {
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
    <nav className="chips" aria-label="Signal chain">
      <DndContext sensors={sensors} collisionDetection={closestCenter} onDragEnd={onDragEnd}>
        <div className="chip-jack">IN</div>
        {props.wdw ? <Wdw wdw={props.wdw} props={props} /> : <List listId="top" blocks={props.blocks} props={props} />}
        <button type="button" className="chip-add" aria-label="Add a block at the end" onClick={props.onAdd}><Plus size={16} /></button>
        <div className="chip-jack">OUT</div>
      </DndContext>
    </nav>
  );
}
