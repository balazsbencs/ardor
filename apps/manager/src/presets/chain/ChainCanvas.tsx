import { DndContext, KeyboardSensor, PointerSensor, closestCenter, useSensor, useSensors, type DragEndEvent } from "@dnd-kit/core";
import { SortableContext, horizontalListSortingStrategy, sortableKeyboardCoordinates, useSortable } from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";
import { ChevronLeft, ChevronRight, Copy, GripVertical, Plus, RotateCcw, Trash2 } from "lucide-react";

import type { PresetBlock } from "../../api/types";
import { Button, IconButton, StatusBadge, Toggle } from "../../components/ui";
import { findEffectDefinition } from "../../effects/catalog";
import type { ValidationIssue } from "../editor/presetValidation";

function titleFor(block: PresetBlock): string {
  return findEffectDefinition(block)?.name ?? block.type;
}

function subtitleFor(block: PresetBlock): string {
  if (block.asset) {
    const segments = block.asset.split("/");
    return segments[segments.length - 1] ?? block.asset;
  }
  const mode = typeof block.params.mode === "string" ? block.params.mode : "";
  return mode || (block.enabled ? "No asset selected" : "Bypassed");
}

type ChainActions = {
  onSelect(blockId?: string): void;
  onAdd(index: number): void;
  onMove(blockId: string, index: number): void;
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
            {blocks.map((block, index) => <SortableChainBlock key={block.id} block={block} index={index} count={blocks.length} selected={selectedBlockId === block.id} issues={issuesFor(block.id)} maxed={maxed} {...actions} />)}
          </SortableContext>
          <div className="chain-terminal chain-terminal--out"><span>OUT</span><small>Output</small></div>
        </div></div>
      </DndContext>
      {blocks.length === 0 && <p className="chain-empty">Start by adding an amp, cabinet, effect, or utility block.</p>}
    </section>
  );
}

function SortableChainBlock({ block, index, count, selected, issues, maxed, onSelect, onAdd, onMove, onToggle, onDuplicate, onReset, onDelete }: {
  block: PresetBlock;
  index: number;
  count: number;
  selected: boolean;
  issues: ValidationIssue[];
  maxed: boolean;
} & ChainActions) {
  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({ id: block.id });
  const error = issues.find(({ severity }) => severity === "error");
  const warning = issues.find(({ severity }) => severity === "warning");
  const definition = findEffectDefinition(block);
  return <div ref={setNodeRef} style={{ transform: CSS.Transform.toString(transform), transition, zIndex: isDragging ? 3 : undefined }} className={`chain-item ${isDragging ? "is-dragging" : ""}`}>
    <article tabIndex={0} className={`chain-block chain-block--${definition?.category ?? "unknown"} ${selected ? "is-selected" : ""} ${!block.enabled ? "is-bypassed" : ""}`} onClick={() => onSelect(block.id)} onKeyDown={(event) => { if (event.key === "Enter" || event.key === " ") { event.preventDefault(); onSelect(block.id); } }}>
      <div className="chain-block__top"><button className="drag-handle" aria-label={`Drag ${titleFor(block)}`} title="Drag to reorder" onClick={(event) => event.stopPropagation()} {...attributes} {...listeners}><GripVertical size={16} aria-hidden="true" /></button><span className="chain-block__ordinal">{index + 1}</span><Toggle label={`${titleFor(block)} enabled`} checked={block.enabled} onChange={(enabled) => onToggle(block.id, enabled)} /></div>
      <div className="chain-block__text"><strong>{titleFor(block)}</strong><small title={subtitleFor(block)}>{subtitleFor(block)}</small></div>
      <div className="chain-block__status">{error && <StatusBadge tone="danger">Fix</StatusBadge>}{!error && warning && <StatusBadge tone="warning">Check</StatusBadge>}{!block.enabled && <StatusBadge>Bypass</StatusBadge>}</div>
      <div className="chain-block__actions" onClick={(event) => event.stopPropagation()}><IconButton label="Move block left" disabled={index === 0} onClick={() => onMove(block.id, index - 1)}><ChevronLeft size={15} /></IconButton><IconButton label="Move block right" disabled={index === count - 1} onClick={() => onMove(block.id, index + 1)}><ChevronRight size={15} /></IconButton><IconButton label="Duplicate block" disabled={maxed} onClick={() => onDuplicate(block.id)}><Copy size={14} /></IconButton><IconButton label="Reset block" onClick={() => onReset(block.id)}><RotateCcw size={14} /></IconButton><IconButton label="Delete block" onClick={() => onDelete(block.id)}><Trash2 size={14} /></IconButton></div>
    </article>
    <ChainInsert index={index + 1} disabled={maxed} onAdd={onAdd} />
  </div>;
}

function ChainInsert({ index, disabled, onAdd }: { index: number; disabled: boolean; onAdd(index: number): void }) {
  return <button className="chain-insert" aria-label={`Add block at position ${index + 1}`} disabled={disabled} onClick={() => onAdd(index)}><Plus size={15} /></button>;
}
