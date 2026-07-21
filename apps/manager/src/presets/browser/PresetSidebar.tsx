import { ChevronLeft, ChevronRight, Search } from "lucide-react";
import { useMemo } from "react";

import type { PresetSlotSummary } from "../../api/types";
import { IconButton, StatusBadge } from "../../components/ui";
import type { PresetLocation } from "../editor/editorTypes";

function slotFor(summaries: PresetSlotSummary[], bank: number, slot: number): PresetSlotSummary | undefined {
  return summaries.find((item) => item.bank === bank && item.slot === slot);
}

export function PresetSidebar({
  summaries,
  selected,
  disabled,
  onSelect,
}: {
  summaries: PresetSlotSummary[];
  selected?: PresetLocation;
  disabled?: boolean;
  onSelect(location: PresetLocation): void;
}) {
  const bank = selected?.bank ?? 0;
  const slots = useMemo(() => [0, 1, 2, 3].map((slot) => slotFor(summaries, bank, slot)), [summaries, bank]);

  const changeBank = (next: number) => {
    if (next < 0 || next > 99) return;
    onSelect({ bank: next, slot: selected?.slot ?? 0 });
  };

  return (
    <aside className="preset-sidebar" aria-label="Preset library">
      <div className="sidebar-heading">
        <div><p className="eyebrow">Preset library</p><h2>Bank {String(bank).padStart(3, "0")}</h2></div>
        <Search size={17} aria-hidden="true" />
      </div>
      <div className="bank-picker" aria-label="Bank picker">
        <IconButton label="Previous bank" disabled={disabled || bank === 0} onClick={() => changeBank(bank - 1)}><ChevronLeft size={18} /></IconButton>
        <input
          aria-label="Bank number"
          type="number"
          min={0}
          max={99}
          value={bank}
          disabled={disabled}
          onChange={(event) => {
            const next = Number(event.target.value);
            if (Number.isInteger(next) && next >= 0 && next <= 99) changeBank(next);
          }}
        />
        <IconButton label="Next bank" disabled={disabled || bank === 99} onClick={() => changeBank(bank + 1)}><ChevronRight size={18} /></IconButton>
      </div>
      <div className="preset-slots">
        {slots.map((summary, slot) => {
          const active = selected?.bank === bank && selected.slot === slot;
          const exists = summary?.exists ?? false;
          const warnings = (summary?.missingAssetCount ?? 0) + (summary?.unsupportedBlockCount ?? 0);
          return (
            <button
              key={slot}
              className={`preset-slot ${active ? "preset-slot--active" : ""}`}
              aria-current={active ? "true" : undefined}
              disabled={disabled}
              onClick={() => onSelect({ bank, slot })}
            >
              <span className="preset-slot__number">{slot + 1}</span>
              <span className="preset-slot__body"><strong>{exists ? summary?.name || "Unnamed" : "Empty slot"}</strong><small>{exists ? `Slot ${slot + 1}` : "Create a preset"}</small></span>
              {warnings > 0 && <StatusBadge tone="warning">{warnings}</StatusBadge>}
            </button>
          );
        })}
      </div>
      <p className="sidebar-note">Choose any of 100 banks. Changes are saved only when you press Save.</p>
    </aside>
  );
}
