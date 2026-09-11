import { DndContext, PointerSensor, closestCenter, useDroppable, useSensor, useSensors, type DragEndEvent } from "@dnd-kit/core";
import { SortableContext, horizontalListSortingStrategy, useSortable } from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";
import { ArrowLeftRight, GripVertical, Merge, Plus, Trash2, Copy, RotateCcw, GitFork } from "lucide-react";

import type { PresetBlock, WdwRouting } from "../../api/types";
import { Button, IconButton, StatusBadge, Toggle } from "../../components/ui";
import { findEffectDefinition } from "../../effects/catalog";
import type { ValidationIssue } from "../editor/presetValidation";
import { isWdwBlockAllowed, wdwLaneLabel } from "../editor/wdwPolicy";

function titleFor(block: PresetBlock): string {
  return findEffectDefinition(block)?.name ?? block.type;
}

function subtitleFor(block: PresetBlock): string {
  if (block.asset) return block.asset.split("/").pop() ?? block.asset;
  const mode = typeof block.params.mode === "string" ? block.params.mode : "";
  return mode || (block.enabled ? "Ready for an asset" : "Bypassed");
}

type WdwActions = {
  onSelect(blockId?: string): void;
  onAdd(lane: "dry" | "wet", index: number): void;
  onMove(lane: "dry" | "wet", blockId: string, index: number): void;
  onToggle(blockId: string, enabled: boolean): void;
  onDuplicate(blockId: string): void;
  onReset(blockId: string): void;
  onDelete(blockId: string): void;
  onMix(lane: "dry" | "wet", key: "levelDb" | "pan" | "width" | "enabled", value: number | boolean): void;
};

export function WdwRoutingCanvas({
  routing,
  selectedBlockId,
  issuesFor,
  maxed,
  ...actions
}: {
  routing: WdwRouting;
  selectedBlockId?: string;
  issuesFor(blockId: string): ValidationIssue[];
  maxed: boolean;
} & WdwActions) {
  const sensors = useSensors(useSensor(PointerSensor, { activationConstraint: { distance: 6 } }));
  const handleDragEnd = ({ active, over }: DragEndEvent) => {
    if (!over) return;
    const activeId = String(active.id);
    const destination = String(over.id);
    const lane = destination.startsWith("wdw:") ? destination.slice(4) as "dry" | "wet"
      : routing.dry.blocks.some(({ id }) => id === destination) ? "dry" : "wet";
    const blocks = routing[lane].blocks;
    const overIndex = blocks.findIndex(({ id }) => id === destination);
    const sourceIndex = blocks.findIndex(({ id }) => id === activeId);
    const index = overIndex < 0 ? blocks.length
      : sourceIndex >= 0 && sourceIndex < overIndex ? overIndex - 1 : overIndex;
    actions.onMove(lane, activeId, index);
  };

  return <section className="chain-panel wdw-panel" aria-label="Wet dry wet routing">
    <div className="chain-panel__heading">
      <div><p className="eyebrow">Wet / dry / wet</p><h2>Shape the contribution lanes</h2></div>
      <StatusBadge tone="info">Two processed paths · no direct input</StatusBadge>
    </div>
    <DndContext sensors={sensors} collisionDetection={closestCenter} onDragEnd={handleDragEnd}>
      <div className="wdw-flow">
        <div className="chain-terminal"><span>IN</span><small>Mono input</small></div>
        <div className="wdw-split"><GitFork size={16} /><small>Split</small></div>
        <div className="wdw-lanes">
          <WdwLane lane="dry" config={routing.dry} otherLaneLength={routing.wet.blocks.length} selectedBlockId={selectedBlockId} issuesFor={issuesFor} maxed={maxed} {...actions} />
          <WdwLane lane="wet" config={routing.wet} otherLaneLength={routing.dry.blocks.length} selectedBlockId={selectedBlockId} issuesFor={issuesFor} maxed={maxed} {...actions} />
        </div>
        <div className="wdw-merge"><Merge size={16} /><small>Join</small></div>
        <div className="chain-terminal chain-terminal--out"><span>OUT</span><small>Stereo</small></div>
      </div>
    </DndContext>
    <p className="wdw-panel__note">Dry is the mono contribution and can be panned. Wet keeps stereo through an optional cab and exposes width for the time-based lane.</p>
  </section>;
}

function WdwLane({ lane, config, otherLaneLength, selectedBlockId, issuesFor, maxed, onSelect, onAdd, onMove, onToggle, onDuplicate, onReset, onDelete, onMix }: {
  lane: "dry" | "wet";
  config: WdwRouting["dry"];
  otherLaneLength: number;
  selectedBlockId?: string;
  issuesFor(blockId: string): ValidationIssue[];
  maxed: boolean;
} & WdwActions) {
  const { setNodeRef, isOver } = useDroppable({ id: `wdw:${lane}` });
  const otherLane = lane === "dry" ? "wet" : "dry";
  return <section className={`wdw-lane wdw-lane--${lane}`}>
    <header className="wdw-lane__heading">
      <div><span className="wdw-lane__name"><b>{lane === "dry" ? "D" : "W"}</b> {lane === "dry" ? "DRY CONTRIBUTION" : "WET CONTRIBUTION"}</span><small>{lane === "dry" ? "1 NAM · optional CAB IR · drive / utility" : "1 NAM · optional CAB IR · time effects after NAM"}</small></div>
      <Toggle label={`${lane} lane enabled`} checked={config.enabled} onChange={(enabled) => onMix(lane, "enabled", enabled)} />
    </header>
    <div className="wdw-lane__mix">
      <label>Level<input type="number" min={-60} max={12} step={0.5} value={config.levelDb} onChange={(event) => onMix(lane, "levelDb", Number(event.target.value))} /><small>dB</small></label>
      {lane === "dry"
        ? <label>Pan<input type="number" min={-1} max={1} step={0.01} value={config.pan} onChange={(event) => onMix(lane, "pan", Number(event.target.value))} /><small>L / R</small></label>
        : <label>Width<input type="number" min={0} max={1} step={0.01} value={config.width ?? 1} onChange={(event) => onMix(lane, "width", Number(event.target.value))} /><small>0–1</small></label>}
    </div>
    <div ref={setNodeRef} className={`wdw-lane__rail ${isOver ? "is-over" : ""}`}>
      <SortableContext items={config.blocks.map(({ id }) => id)} strategy={horizontalListSortingStrategy}>
        {config.blocks.map((block, index) => <WdwBlock key={block.id} block={block} index={index} count={config.blocks.length} lane={lane} otherLane={otherLane} otherLaneLength={otherLaneLength} canDuplicate={!((block.type === "nam" || block.type === "cab") && config.blocks.some((candidate) => candidate.id !== block.id && candidate.type === block.type))} selected={selectedBlockId === block.id} issues={issuesFor(block.id)} onSelect={onSelect} onMove={onMove} onToggle={onToggle} onDuplicate={onDuplicate} onReset={onReset} onDelete={onDelete} />)}
      </SortableContext>
      <button className="wdw-add" disabled={maxed || config.blocks.length >= 10} onClick={() => onAdd(lane, config.blocks.length)}><Plus size={14} /> Add block</button>
    </div>
  </section>;
}

function WdwBlock({ block, index, count, lane, otherLane, otherLaneLength, canDuplicate, selected, issues, onSelect, onMove, onToggle, onDuplicate, onReset, onDelete }: {
  block: PresetBlock;
  index: number;
  count: number;
  lane: "dry" | "wet";
  otherLane: "dry" | "wet";
  otherLaneLength: number;
  canDuplicate: boolean;
  selected: boolean;
  issues: ValidationIssue[];
  onSelect(blockId?: string): void;
  onMove(lane: "dry" | "wet", blockId: string, index: number): void;
  onToggle(blockId: string, enabled: boolean): void;
  onDuplicate(blockId: string): void;
  onReset(blockId: string): void;
  onDelete(blockId: string): void;
}) {
  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({ id: block.id });
  const definition = findEffectDefinition(block);
  const canMoveToOtherLane = isWdwBlockAllowed(otherLane, block);
  const error = issues.some(({ severity }) => severity === "error");
  const warning = !error && issues.some(({ severity }) => severity === "warning");
  return <article ref={setNodeRef} style={{ transform: CSS.Transform.toString(transform), transition }} className={`wdw-block wdw-block--${definition?.category ?? "unknown"} ${selected ? "is-selected" : ""} ${isDragging ? "is-dragging" : ""} ${!block.enabled ? "is-bypassed" : ""} ${error ? "has-error" : warning ? "has-warning" : ""}`} onClick={() => onSelect(block.id)}>
    <div className="wdw-block__top"><button className="drag-handle" aria-label={`Drag ${titleFor(block)}`} {...attributes} {...listeners}><GripVertical size={14} /></button><span>{index + 1}</span><Toggle label={`${titleFor(block)} enabled`} checked={block.enabled} onChange={(enabled) => onToggle(block.id, enabled)} /></div>
    <strong>{titleFor(block)}</strong><small>{subtitleFor(block)}</small>
    <div className="wdw-block__status">{error && <StatusBadge tone="danger">Fix</StatusBadge>}{!error && warning && <StatusBadge tone="warning">Check</StatusBadge>}{!block.enabled && <StatusBadge>Bypass</StatusBadge>}</div>
    <footer onClick={(event) => event.stopPropagation()}><IconButton label={`Move ${titleFor(block)} left`} disabled={index === 0} onClick={() => onMove(lane, block.id, index - 1)}><ArrowLeftRight size={13} /></IconButton><IconButton label={canMoveToOtherLane ? `Move ${titleFor(block)} to ${wdwLaneLabel(otherLane)} lane` : `${titleFor(block)} stays on the ${wdwLaneLabel(lane)} lane`} disabled={!canMoveToOtherLane} onClick={() => onMove(otherLane, block.id, otherLaneLength)}><ArrowLeftRight size={13} /></IconButton><IconButton label={`Duplicate ${titleFor(block)}`} disabled={!canDuplicate} onClick={() => onDuplicate(block.id)}><Copy size={13} /></IconButton><IconButton label={`Reset ${titleFor(block)}`} onClick={() => onReset(block.id)}><RotateCcw size={13} /></IconButton><IconButton label={`Delete ${titleFor(block)}`} onClick={() => onDelete(block.id)}><Trash2 size={13} /></IconButton></footer>
  </article>;
}
