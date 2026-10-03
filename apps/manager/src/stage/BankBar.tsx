import { ChevronLeft, ChevronRight } from "lucide-react";

import type { Preset } from "../api/types";
import { useDeviceSession } from "../connection/deviceSession";
import { IconButton } from "../components/ui";
import type { PresetLocation } from "../presets/editor/editorTypes";
import { missingPaths } from "../assets/assetRefs";
import { bankLabel } from "../ui/format";
import { PresetTile } from "./PresetTile";
import { useBankPresets } from "./useBankPresets";

export function BankBar({ bank, editing, live, dirty, draft, disabled, onBank, onOpen }: {
  bank: number; editing: PresetLocation; live?: PresetLocation; dirty: boolean; draft: Preset; disabled: boolean;
  onBank(bank: number): void; onOpen(location: PresetLocation): void;
}) {
  const session = useDeviceSession();
  const presets = useBankPresets(bank, { location: editing, preset: draft });
  const inventory = { models: session.models, irs: session.irs, reverbIrs: session.reverbIrs };
  return (
    <section className="bankbar" aria-label="Bank and presets">
      <div className="bank-step">
        <IconButton label="Previous bank" disabled={disabled || bank === 0} onClick={() => onBank(bank - 1)}><ChevronLeft size={18} /></IconButton>
        <div className="bank-step__mid"><b>{bankLabel(bank)}</b></div>
        <IconButton label="Next bank" disabled={disabled || bank === 99} onClick={() => onBank(bank + 1)}><ChevronRight size={18} /></IconButton>
      </div>
      <div className="bank-tiles">
        {[0, 1, 2, 3].map((slot) => {
          const preset = presets.get(slot);
          const isEditing = editing.bank === bank && editing.slot === slot;
          return <PresetTile key={slot} slot={slot} preset={preset}
            live={live?.bank === bank && live.slot === slot} editing={isEditing} dirty={isEditing && dirty}
            missing={preset ? missingPaths(preset, inventory).size > 0 : false} onOpen={() => onOpen({ bank, slot })} />;
        })}
      </div>
    </section>
  );
}
