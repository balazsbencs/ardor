import * as Dialog from "@radix-ui/react-dialog";
import { Search, X } from "lucide-react";
import { useMemo, useState } from "react";

import { allEffectDefinitions } from "../../effects/catalog";
import type { EffectCategory, EffectDefinition } from "../../effects/types";
import { Button, IconButton, StatusBadge } from "../../components/ui";

const categories: Array<{ value: "all" | EffectCategory; label: string }> = [
  { value: "all", label: "All" }, { value: "amp", label: "Amp" }, { value: "cabinet", label: "Cab" },
  { value: "dynamics", label: "Dynamics" }, { value: "eq", label: "EQ" }, { value: "modulation", label: "Mod" },
  { value: "delay", label: "Delay" }, { value: "reverb", label: "Reverb" },
];

function matches(definition: EffectDefinition, query: string) {
  const haystack = [definition.name, definition.description, ...(definition.aliases ?? [])].join(" ").toLowerCase();
  return haystack.includes(query.toLowerCase());
}

function categoryLabel(category: EffectCategory): string {
  return category === "cabinet" ? "Cabinet" : category[0].toUpperCase() + category.slice(1);
}

export function BlockBrowser({
  open,
  onOpenChange,
  onChoose,
  disabledIds = new Map<string, string>(),
}: {
  open: boolean;
  onOpenChange(open: boolean): void;
  onChoose(definition: EffectDefinition): void;
  disabledIds?: Map<string, string>;
}) {
  const [query, setQuery] = useState("");
  const [category, setCategory] = useState<"all" | EffectCategory>("all");
  const definitions = useMemo(() => allEffectDefinitions().filter((definition) =>
    (category === "all" || definition.category === category) && matches(definition, query),
  ), [category, query]);

  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal>
        <Dialog.Overlay className="dialog-overlay" />
        <Dialog.Content className="block-browser-dialog" aria-describedby={undefined}>
          <div className="dialog-header"><div><p className="eyebrow">Add to chain</p><Dialog.Title>Choose an effect</Dialog.Title></div><Dialog.Close asChild><IconButton label="Close effect browser"><X size={20} /></IconButton></Dialog.Close></div>
          <label className="search-field"><Search size={17} /><input autoFocus placeholder="Search effects" value={query} onChange={(event) => setQuery(event.target.value)} /></label>
          <div className="category-tabs" role="tablist" aria-label="Effect categories">
            {categories.map((item) => <button key={item.value} role="tab" aria-selected={category === item.value} className={category === item.value ? "is-active" : ""} onClick={() => setCategory(item.value)}>{item.label}</button>)}
          </div>
          <div className="effect-grid">
            {definitions.map((definition) => {
              const reason = disabledIds.get(definition.id);
              return <article className={`effect-definition effect-definition--${definition.category}`} key={definition.id}>
                <div><StatusBadge tone="info">{categoryLabel(definition.category)}</StatusBadge><h3>{definition.name}</h3><p>{definition.description}</p></div>
                {reason && <small className="effect-definition__reason">{reason}</small>}
                <Button disabled={Boolean(reason)} onClick={() => onChoose(definition)}>Add</Button>
              </article>;
            })}
          </div>
          {definitions.length === 0 && <p className="empty-inline">No matching effects.</p>}
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
