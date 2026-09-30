import * as Dialog from "@radix-ui/react-dialog";
import { Plus, Search, X } from "lucide-react";
import { useMemo, useState } from "react";

import { IconButton } from "../components/ui";
import { allEffectDefinitions } from "../effects/catalog";
import type { EffectDefinition } from "../effects/types";
import { PortalSurface } from "../theme/surface";
import { familyOf, type Family } from "../ui/family";
import { MODULE_CODES } from "./codes";
import "./modules.css";

const GROUPS: Array<{ family: Family; label: string }> = [
  { family: "amp", label: "Amp and drive" },
  { family: "cab", label: "Cab" },
  { family: "util", label: "Dynamics and tone" },
  { family: "mod", label: "Modulation" },
  { family: "dly", label: "Delay" },
  { family: "rev", label: "Reverb" },
];

const matches = (definition: EffectDefinition, query: string) =>
  [definition.name, definition.description, ...(definition.aliases ?? []), MODULE_CODES[definition.id] ?? ""]
    .join(" ")
    .toLowerCase()
    .includes(query.toLowerCase());

type ModuleDrawerProps = {
  open: boolean;
  where: string;
  disabledIds: Map<string, string>;
  onOpenChange(open: boolean): void;
  onChoose(definition: EffectDefinition): void;
};

export function ModuleDrawer({ open, where, disabledIds, onOpenChange, onChoose }: ModuleDrawerProps) {
  const [query, setQuery] = useState("");
  const [family, setFamily] = useState<Family | "all">("all");
  const total = useMemo(() => allEffectDefinitions().length, []);
  const definitions = useMemo(
    () => allEffectDefinitions().filter((d) => (family === "all" || familyOf(d.blockType) === family) && matches(d, query)),
    [family, query],
  );
  const choose = (definition: EffectDefinition) => {
    onChoose(definition);
    setQuery("");
  };
  const pickFirst = () => {
    const first = definitions.find((d) => !disabledIds.has(d.id));
    if (first) choose(first);
  };
  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal>
        <PortalSurface>
          <Dialog.Overlay className="mods-scrim" />
          <Dialog.Content className="mods" aria-describedby={undefined}>
            <div className="mods__head">
              <div className="mods__title">
                <div>
                  <Dialog.Title>Add block</Dialog.Title>
                  <p>{where}</p>
                </div>
                <Dialog.Close asChild>
                  <IconButton label="Close"><X size={18} /></IconButton>
                </Dialog.Close>
              </div>
              <label className="search">
                <Search size={17} />
                <input
                  type="search"
                  aria-label="Search blocks"
                  placeholder={`Search ${total} blocks`}
                  value={query}
                  autoFocus
                  onChange={(e) => setQuery(e.target.value)}
                  onKeyDown={(e) => { if (e.key === "Enter") pickFirst(); }}
                />
              </label>
              <div className="fams" role="group" aria-label="Families">
                <button type="button" aria-pressed={family === "all"} onClick={() => setFamily("all")}>All</button>
                {GROUPS.map((g) => (
                  <button key={g.family} type="button" className={`fam-${g.family}`} aria-pressed={family === g.family} onClick={() => setFamily(g.family)}>
                    {g.label}
                  </button>
                ))}
              </div>
            </div>
            <div className="mods__list">
              {GROUPS.map((group) => {
                const rows = definitions.filter((d) => familyOf(d.blockType) === group.family);
                if (!rows.length) return null;
                return (
                  <div key={group.family} role="group" aria-label={group.label}>
                    <div className="mods__grp"><span>{group.label}</span><span>{rows.length}</span></div>
                    {rows.map((d) => {
                      const reason = disabledIds.get(d.id);
                      return (
                        <button key={d.id} type="button" className={`mod-row fam-${group.family}`} disabled={Boolean(reason)} onClick={() => choose(d)}>
                          <span className="code">{MODULE_CODES[d.id]}</span>
                          <span><b>{d.name}</b><small>{reason ?? d.description}</small></span>
                          <span className="plus"><Plus size={16} /></span>
                        </button>
                      );
                    })}
                  </div>
                );
              })}
              {definitions.length === 0 && <p className="lb-note">No block matches. Try a family name, for example delay.</p>}
            </div>
          </Dialog.Content>
        </PortalSurface>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
