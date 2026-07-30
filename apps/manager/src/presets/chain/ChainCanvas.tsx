import { DndContext, KeyboardSensor, PointerSensor, closestCenter, useDroppable, useSensor, useSensors, type DragEndEvent } from "@dnd-kit/core";
import { SortableContext, horizontalListSortingStrategy, sortableKeyboardCoordinates, useSortable } from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";
import { ArrowLeftRight, ChevronLeft, ChevronRight, Copy, GitFork, GripVertical, Merge, Plus, RotateCcw, Trash2 } from "lucide-react";

import type { PresetBlock } from "../../api/types";
import { Button, IconButton, StatusBadge, Toggle } from "../../components/ui";
import { findEffectDefinition } from "../../effects/catalog";
import type { ValidationIssue } from "../editor/presetValidation";

function titleFor(block: PresetBlock): string {
  return findEffectDefinition(block)?.name ?? block.type;
}

function subtitleFor(block: PresetBlock): string {
  if (block.type === "dualAmp") return "Parallel NAM → IR";
  if (block.type === "dualRig") return "Independent left / right chains";
  if (block.asset) {
    const segments = block.asset.split("/");
    return segments[segments.length - 1] ?? block.asset;
  }
  const mode = typeof block.params.mode === "string" ? block.params.mode : "";
  return mode || (block.enabled ? "No asset selected" : "Bypassed");
}

function filename(value: unknown): string {
  if (typeof value !== "string" || value.length === 0) return "Choose asset";
  return value.split("/").pop() ?? value;
}

type ChainActions = {
  onSelect(blockId?: string): void;
  onAdd(index: number): void;
  onMove(blockId: string, index: number): void;
  onLaneAdd(rigId: string, lane: "left" | "right", index: number): void;
  onLaneMove(rigId: string, blockId: string, lane: "left" | "right", index: number): void;
  onToggle(blockId: string, enabled: boolean): void;
  onDuplicate(blockId: string): void;
  onReset(blockId: string): void;
  onDelete(blockId: string): void;
};

export function ChainCanvas({ blocks, selectedBlockId, issuesFor, maxed, ...actions }: {
  blocks: PresetBlock[];
  selectedBlockId?: string;
  issuesFor(blockId: string): ValidationIssue[];
  maxed: boolean;
} & ChainActions) {
  const sensors = useSensors(
    useSensor(PointerSensor, { activationConstraint: { distance: 6 } }),
    useSensor(KeyboardSensor, { coordinateGetter: sortableKeyboardCoordinates }),
  );
  const handleDragEnd = ({ active, over }: DragEndEvent) => {
    if (!over || active.id === over.id) return;
    const destination = blocks.findIndex(({ id }) => id === String(over.id));
    if (destination >= 0) actions.onMove(String(active.id), destination);
  };

  return (
    <section className="chain-panel" aria-label="Signal chain">
      <div className="chain-panel__heading"><div><p className="eyebrow">Signal chain</p><h2>Shape the route</h2></div><Button variant="secondary" disabled={maxed} onClick={() => actions.onAdd(blocks.length)}><Plus size={16} /> Add block</Button></div>
      <DndContext sensors={sensors} collisionDetection={closestCenter} onDragEnd={handleDragEnd}>
        <div className="chain-scroll"><div className="chain-canvas">
          <div className="chain-terminal"><span>IN</span><small>Input</small></div>
          <ChainInsert index={0} disabled={maxed} onAdd={actions.onAdd} />
          <SortableContext items={blocks.map(({ id }) => id)} strategy={horizontalListSortingStrategy}>
            {blocks.map((block, index) => <SortableChainBlock key={block.id} block={block} index={index} count={blocks.length} selected={selectedBlockId === block.id} selectedBlockId={selectedBlockId} issues={issuesFor(block.id)} issuesFor={issuesFor} maxed={maxed} {...actions} />)}
          </SortableContext>
          <div className="chain-terminal chain-terminal--out"><span>OUT</span><small>Output</small></div>
        </div></div>
      </DndContext>
      {blocks.length === 0 && <p className="chain-empty">Start by adding an amp, cabinet, effect, or utility block.</p>}
    </section>
  );
}

function SortableChainBlock({ block, index, count, selected, selectedBlockId, issues, issuesFor, maxed, onSelect, onAdd, onMove, onLaneAdd, onLaneMove, onToggle, onDuplicate, onReset, onDelete }: {
  block: PresetBlock;
  index: number;
  count: number;
  selected: boolean;
  selectedBlockId?: string;
  issues: ValidationIssue[];
  issuesFor(blockId: string): ValidationIssue[];
  maxed: boolean;
} & ChainActions) {
  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({ id: block.id });
  const laneIssues = block.type === "dualRig" && block.lanes
    ? [...block.lanes.left.blocks, ...block.lanes.right.blocks].flatMap(({ id }) => issuesFor(id))
    : [];
  const allIssues = [...issues, ...laneIssues];
  const error = allIssues.find(({ severity }) => severity === "error");
  const warning = allIssues.find(({ severity }) => severity === "warning");
  const definition = findEffectDefinition(block);
  return <div ref={setNodeRef} style={{ transform: CSS.Transform.toString(transform), transition, zIndex: isDragging ? 3 : undefined }} className={`chain-item ${isDragging ? "is-dragging" : ""}`}>
    <article tabIndex={0} className={`chain-block chain-block--${definition?.category ?? "unknown"} ${selected ? "is-selected" : ""} ${!block.enabled ? "is-bypassed" : ""}`} onClick={() => onSelect(block.id)} onKeyDown={(event) => { if (event.key === "Enter" || event.key === " ") { event.preventDefault(); onSelect(block.id); } }}>
      <div className="chain-block__top"><button className="drag-handle" aria-label={`Drag ${titleFor(block)}`} title="Drag to reorder" onClick={(event) => event.stopPropagation()} {...attributes} {...listeners}><GripVertical size={16} aria-hidden="true" /></button><span className="chain-block__ordinal">{index + 1}</span><Toggle label={`${titleFor(block)} enabled`} checked={block.enabled} onChange={(enabled) => onToggle(block.id, enabled)} /></div>
      <div className="chain-block__text"><strong>{titleFor(block)}</strong>{block.type === "dualAmp"
        ? <div className="dual-amp-lanes">
          <div><span>L</span><small>{filename(block.params.leftNamAsset)}</small><i>→</i><small>{filename(block.params.leftIrAsset)}</small></div>
          <div><span>R</span><small>{filename(block.params.rightNamAsset)}</small><i>→</i><small>{filename(block.params.rightIrAsset)}</small></div>
        </div>
        : block.type === "dualRig" && block.lanes
          ? <DualRigLanes rig={block} selectedBlockId={selectedBlockId} maxed={false}
              issuesFor={issuesFor}
              onSelect={onSelect} onLaneAdd={onLaneAdd} onLaneMove={onLaneMove}
              onToggle={onToggle} onDuplicate={onDuplicate} onDelete={onDelete} />
        : <small title={subtitleFor(block)}>{subtitleFor(block)}</small>}</div>
      <div className="chain-block__status">{block.enabled && block.type === "nam" && <StatusBadge tone="info">Stereo → Mono</StatusBadge>}{block.enabled && (block.type === "dualAmp" || block.type === "dualRig") && <StatusBadge tone="info">Parallel Stereo</StatusBadge>}{error && <StatusBadge tone="danger">Fix</StatusBadge>}{!error && warning && <StatusBadge tone="warning">Check</StatusBadge>}{!block.enabled && <StatusBadge>Bypass</StatusBadge>}</div>
      <div className="chain-block__actions" onClick={(event) => event.stopPropagation()}><IconButton label="Move block left" disabled={index === 0} onClick={() => onMove(block.id, index - 1)}><ChevronLeft size={15} /></IconButton><IconButton label="Move block right" disabled={index === count - 1} onClick={() => onMove(block.id, index + 1)}><ChevronRight size={15} /></IconButton><IconButton label="Duplicate block" disabled={maxed || block.type === "dualAmp" || block.type === "dualRig"} onClick={() => onDuplicate(block.id)}><Copy size={14} /></IconButton><IconButton label="Reset block" onClick={() => onReset(block.id)}><RotateCcw size={14} /></IconButton><IconButton label="Delete block" onClick={() => onDelete(block.id)}><Trash2 size={14} /></IconButton></div>
    </article>
    <ChainInsert index={index + 1} disabled={maxed} onAdd={onAdd} />
  </div>;
}

function DualRigLanes({ rig, selectedBlockId, maxed, issuesFor, onSelect, onLaneAdd, onLaneMove, onToggle, onDuplicate, onDelete }: {
  rig: PresetBlock;
  selectedBlockId?: string;
  maxed: boolean;
  issuesFor(blockId: string): ValidationIssue[];
  onSelect(blockId?: string): void;
  onLaneAdd(rigId: string, lane: "left" | "right", index: number): void;
  onLaneMove(rigId: string, blockId: string, lane: "left" | "right", index: number): void;
  onToggle(blockId: string, enabled: boolean): void;
  onDuplicate(blockId: string): void;
  onDelete(blockId: string): void;
}) {
  const lanes = rig.lanes!;
  const sensors = useSensors(
    useSensor(PointerSensor, { activationConstraint: { distance: 6 } }),
    useSensor(KeyboardSensor, { coordinateGetter: sortableKeyboardCoordinates }),
  );
  const handleDragEnd = ({ active, over }: DragEndEvent) => {
    if (!over) return;
    const destinationLane = String(over.id) === `${rig.id}:left`
      || lanes.left.blocks.some(({ id }) => id === String(over.id)) ? "left" : "right";
    const destinationBlocks = lanes[destinationLane].blocks;
    const targetIndex = destinationBlocks.findIndex(({ id }) => id === String(over.id));
    onLaneMove(rig.id, String(active.id), destinationLane,
      targetIndex >= 0 ? targetIndex : destinationBlocks.length);
  };
  return <div className="dual-rig-editor" onClick={(event) => event.stopPropagation()}>
    <div className="dual-rig-anchor"><GitFork size={16} /><small>Split</small></div>
    <DndContext sensors={sensors} collisionDetection={closestCenter} onDragEnd={handleDragEnd}>
      <div className="dual-rig-lanes">
        <RigLane rigId={rig.id} lane="left" blocks={lanes.left.blocks} selectedBlockId={selectedBlockId}
          otherLaneLength={lanes.right.blocks.length} maxed={maxed} onSelect={onSelect} onLaneAdd={onLaneAdd} onLaneMove={onLaneMove}
          issuesFor={issuesFor} onToggle={onToggle} onDuplicate={onDuplicate} onDelete={onDelete} />
        <RigLane rigId={rig.id} lane="right" blocks={lanes.right.blocks} selectedBlockId={selectedBlockId}
          otherLaneLength={lanes.left.blocks.length} maxed={maxed} onSelect={onSelect} onLaneAdd={onLaneAdd} onLaneMove={onLaneMove}
          issuesFor={issuesFor} onToggle={onToggle} onDuplicate={onDuplicate} onDelete={onDelete} />
      </div>
    </DndContext>
    <div className="dual-rig-anchor"><Merge size={16} /><small>Merge</small></div>
  </div>;
}

function RigLane({ rigId, lane, blocks, selectedBlockId, otherLaneLength, maxed, issuesFor, onSelect, onLaneAdd, onLaneMove, onToggle, onDuplicate, onDelete }: {
  rigId: string;
  lane: "left" | "right";
  blocks: PresetBlock[];
  selectedBlockId?: string;
  otherLaneLength: number;
  maxed: boolean;
  issuesFor(blockId: string): ValidationIssue[];
  onSelect(blockId?: string): void;
  onLaneAdd(rigId: string, lane: "left" | "right", index: number): void;
  onLaneMove(rigId: string, blockId: string, lane: "left" | "right", index: number): void;
  onToggle(blockId: string, enabled: boolean): void;
  onDuplicate(blockId: string): void;
  onDelete(blockId: string): void;
}) {
  const { setNodeRef, isOver } = useDroppable({ id: `${rigId}:${lane}` });
  const otherLane = lane === "left" ? "right" : "left";
  return <section className={`dual-rig-lane dual-rig-lane--${lane} ${isOver ? "is-over" : ""}`}>
    <header><span><b>{lane === "left" ? "L" : "R"}</b> {lane.toUpperCase()}</span><small>Output {lane === "left" ? "1" : "2"}</small></header>
    <div ref={setNodeRef} className="dual-rig-lane__rail">
      <SortableContext items={blocks.map(({ id }) => id)} strategy={horizontalListSortingStrategy}>
        {blocks.map((block) => <RigLaneBlock key={block.id} block={block} selected={selectedBlockId === block.id}
          otherLane={otherLane} rigId={rigId} otherLaneLength={otherLaneLength}
          issues={issuesFor(block.id)}
          onSelect={onSelect} onLaneMove={onLaneMove} onToggle={onToggle}
          onDuplicate={onDuplicate} onDelete={onDelete} />)}
      </SortableContext>
      <button className="dual-rig-add" disabled={maxed || blocks.length >= 10}
        aria-label={`Add effect to ${lane} lane`} onClick={() => onLaneAdd(rigId, lane, blocks.length)}>
        <Plus size={14} /> Add
      </button>
    </div>
  </section>;
}

function RigLaneBlock({ block, selected, otherLane, rigId, otherLaneLength, issues, onSelect, onLaneMove, onToggle, onDuplicate, onDelete }: {
  block: PresetBlock;
  selected: boolean;
  otherLane: "left" | "right";
  rigId: string;
  otherLaneLength: number;
  issues: ValidationIssue[];
  onSelect(blockId?: string): void;
  onLaneMove(rigId: string, blockId: string, lane: "left" | "right", index: number): void;
  onToggle(blockId: string, enabled: boolean): void;
  onDuplicate(blockId: string): void;
  onDelete(blockId: string): void;
}) {
  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({ id: block.id });
  const error = issues.some(({ severity }) => severity === "error");
  const warning = !error && issues.some(({ severity }) => severity === "warning");
  return <article ref={setNodeRef} style={{ transform: CSS.Transform.toString(transform), transition }}
      className={`dual-rig-effect ${selected ? "is-selected" : ""} ${isDragging ? "is-dragging" : ""} ${!block.enabled ? "is-bypassed" : ""} ${error ? "has-error" : warning ? "has-warning" : ""}`}
      onClick={() => onSelect(block.id)}>
    <div><button className="drag-handle" aria-label={`Drag ${titleFor(block)}`} {...attributes} {...listeners}><GripVertical size={13} /></button><strong>{titleFor(block)}</strong><Toggle label={`${titleFor(block)} enabled`} checked={block.enabled} onChange={(enabled) => onToggle(block.id, enabled)} /></div>
    <small>{block.asset ? filename(block.asset) : subtitleFor(block)}{error ? " · Fix" : warning ? " · Check" : ""}</small>
    <footer>
      <IconButton label="Move to other lane" onClick={() => onLaneMove(rigId, block.id, otherLane, otherLaneLength)}><ArrowLeftRight size={13} /></IconButton>
      <IconButton label="Duplicate lane block" onClick={() => onDuplicate(block.id)}><Copy size={13} /></IconButton>
      <IconButton label="Delete lane block" onClick={() => onDelete(block.id)}><Trash2 size={13} /></IconButton>
    </footer>
  </article>;
}

function ChainInsert({ index, disabled, onAdd }: { index: number; disabled: boolean; onAdd(index: number): void }) {
  return <button className="chain-insert" aria-label={`Add block at position ${index + 1}`} disabled={disabled} onClick={() => onAdd(index)}><Plus size={15} /></button>;
}
