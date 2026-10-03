import type { Preset, PresetBlock } from "../api/types";
import { cx } from "../components/ui";
import { familyOf } from "../ui/family";
import { slotLabel } from "../ui/format";
import { Tag } from "../ui/Tag";
import { stripCode } from "./codes";
import "./bank.css";

function Strip({ blocks }: { blocks: PresetBlock[] }) {
  return <div className="strip" aria-hidden="true">{blocks.map((block) => block.lanes
    ? <span key={block.id} className="fam-amp w2">RIG {block.lanes.left.blocks.length}+{block.lanes.right.blocks.length}</span>
    : <span key={block.id} className={cx(`fam-${familyOf(block.type)}`, !block.enabled && "off", ["amp", "cab"].includes(familyOf(block.type)) && "w2")}>{stripCode(block)}</span>)}</div>;
}

export function PresetTile({ slot, preset, live, editing, dirty, missing, onOpen }: {
  slot: number; preset?: Preset; live: boolean; editing: boolean; dirty: boolean; missing: boolean; onOpen(): void;
}) {
  if (!preset) {
    return <button type="button" className={cx("tile", "is-empty", editing && "is-edit")} onClick={onOpen} aria-label={`${slotLabel(slot)}, empty slot`}>
      <div className="tile__top"><span className="tile__fs">{slotLabel(slot)}</span></div><div className="tile__name">Empty slot</div></button>;
  }
  const blocks = preset.routing === "wdw" && preset.wdw ? [...preset.wdw.dry.blocks, ...preset.wdw.wet.blocks] : preset.blocks;
  return (
    <button type="button" className={cx("tile", live && "is-live", editing && "is-edit")} onClick={onOpen} aria-pressed={editing}
      aria-label={`${preset.name}, ${slotLabel(slot)}${live ? ", live on the pedal" : ""}${editing ? ", open in the editor" : ""}`}>
      <div className="tile__top"><span className="tile__fs">{slotLabel(slot)}</span>
        <span className="tile__tags">{live && <Tag tone="ink">LIVE</Tag>}{dirty && <Tag tone="warn">EDITED</Tag>}{missing && <Tag tone="warn" title="Uses a file that is not on the pedal">MISSING FILE</Tag>}</span></div>
      <div className="tile__name">{preset.name}</div>
      <Strip blocks={blocks} />
    </button>
  );
}
