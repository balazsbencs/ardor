import { GripVertical, Power } from "lucide-react";
import type { CSSProperties, HTMLAttributes, KeyboardEvent, Ref } from "react";

import type { PresetBlock } from "../api/types";
import { cx } from "../components/ui";
import { findEffectDefinition } from "../effects/catalog";
import { displayValue } from "../effects/display";
import type { NumberControl } from "../effects/types";
import { eqStateFor, responsePath } from "../presets/inspector/EqResponseGraph";
import type { ValidationIssue } from "../presets/editor/presetValidation";
import { capFor, familyOf } from "../ui/family";
import { fileStem } from "../ui/format";
import { Tag } from "../ui/Tag";
import { mainControls } from "./mainValues";
import "./stage.css";

export type BlockCardProps = {
  block: PresetBlock;
  selected: boolean;
  issues: ValidationIssue[];
  missingFile: boolean;
  sceneOwnsEnabled: boolean;
  laneTag?: "A" | "B" | "DRY" | "WET";
  onSelect(): void;
  onToggle(): void;
  onNudge(direction: -1 | 1): void;
  handleProps?: HTMLAttributes<HTMLElement>;
  innerRef?: Ref<HTMLDivElement>;
  style?: CSSProperties;
  dragging?: boolean;
};

export function blockTitle(block: PresetBlock): string {
  if (block.asset) return fileStem(block.asset.split("/").pop() ?? block.asset);
  return findEffectDefinition(block)?.name ?? block.type;
}

function Value({ control, value }: { control: NumberControl; value: number }) {
  const fraction = control.maximum === control.minimum ? 0 : (value - control.minimum) / (control.maximum - control.minimum);
  return <>
    <div className="blk__p"><span>{control.label}</span> <b>{displayValue(control, value)}</b></div>
    <div className="blk__bar"><i style={{ width: `${Math.min(100, Math.max(0, fraction * 100)).toFixed(1)}%` }} /></div>
  </>;
}

export function BlockCard({ block, selected, issues, missingFile, sceneOwnsEnabled, laneTag, onSelect, onToggle, onNudge, handleProps, innerRef, style, dragging }: BlockCardProps) {
  const definition = findEffectDefinition(block);
  const family = familyOf(block.type);
  const title = blockTitle(block);
  const error = issues.find(({ severity }) => severity === "error");
  const warning = issues.find(({ severity }) => severity === "warning");
  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    if (event.altKey && (event.key === "ArrowLeft" || event.key === "ArrowRight")) {
      event.preventDefault();
      onNudge(event.key === "ArrowRight" ? 1 : -1);
    } else if (event.key === "Enter" && event.target === event.currentTarget) onSelect();
  };
  const values = definition ? mainControls(definition).map((control) => {
    const raw = block.params[control.key];
    return <Value key={control.key} control={control} value={typeof raw === "number" ? raw : control.defaultValue} />;
  }) : [];
  return (
    <div ref={innerRef} style={{ ...style, viewTransitionName: `block-${block.id}` }} role="group" tabIndex={0} data-block-id={block.id}
      aria-label={`${title}, ${capFor(block.type)}, ${block.enabled ? "on" : "bypassed"}`} aria-current={selected || undefined}
      className={cx("blk", `fam-${family}`, block.enabled ? "is-on" : "is-off", selected && "is-sel", dragging && "is-dragging")}
      onClick={onSelect} onKeyDown={onKeyDown}>
      <div className="blk__cap" {...handleProps}>
        <span>{capFor(block.type)}{sceneOwnsEnabled && <> <Tag tone="scene">SCENE</Tag></>}</span>
        <span className="blk__grip" aria-hidden="true"><GripVertical size={16} /></span>
      </div>
      <button type="button" className="blk__pow" aria-label={`${block.enabled ? "Bypass" : "Turn on"} ${title}`}
        title={block.enabled ? "Bypass (B)" : "Turn on (B)"} onClick={(event) => { event.stopPropagation(); onToggle(); }}><Power size={15} /></button>
      <div className="blk__name">{title}</div>
      <div className="blk__sub">
        {missingFile ? <Tag tone="warn">FILE MISSING</Tag> : block.asset ? definition?.name : null}
        {error ? <Tag tone="danger" title={error.message}>FIX</Tag> : warning ? <Tag tone="line" title={warning.message}>CHECK</Tag> : null}
        {laneTag && <Tag tone="line">{laneTag}</Tag>}
      </div>
      {!block.enabled ? <span className="blk__off">OFF</span>
        : block.type === "eq" ? <svg className="blk__eq" viewBox="0 0 150 64" preserveAspectRatio="none" aria-hidden="true"><path d={responsePath(eqStateFor(block), 150, 64)} /></svg>
          : <div className="blk__params">{values}</div>}
    </div>
  );
}
