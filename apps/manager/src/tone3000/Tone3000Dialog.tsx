import * as Dialog from "@radix-ui/react-dialog";
import { Download, ExternalLink, LoaderCircle, X } from "lucide-react";

import { Button, StatusBadge } from "../components/ui";
import { PortalSurface } from "../theme/surface";
import type { Tone3000Selection } from "./types";
import { Tone3000Brand } from "./Tone3000Brand";

export { Tone3000Brand };

export type Tone3000Phase = "idle" | "intro" | "waiting" | "loading" | "detail" | "installing";

function label(value: string): string {
  return value.split("-").map((part) => part.charAt(0).toUpperCase() + part.slice(1)).join(" + ");
}

function architectureLabel(value: "1" | "2" | "custom" | null): string {
  if (value === "1") return "A1";
  if (value === "2") return "A2";
  if (value === "custom") return "Custom";
  return "NAM";
}

export function Tone3000Dialog({
  phase,
  selection,
  selectedModelId,
  onSelectedModelId,
  onContinue,
  onCancel,
  onInstall,
}: {
  phase: Tone3000Phase;
  selection?: Tone3000Selection;
  selectedModelId?: number;
  onSelectedModelId(id: number): void;
  onContinue(): void;
  onCancel(): void;
  onInstall(): void;
}) {
  const isWorking = phase === "loading" || phase === "installing";
  const refuseDismiss = (event: Event) => { if (isWorking) event.preventDefault(); };

  return (
    <Dialog.Root open={phase !== "idle"} onOpenChange={(open) => { if (!open && !isWorking) onCancel(); }}>
      <Dialog.Portal>
        <PortalSurface>
          <Dialog.Overlay className="tone3000-overlay" />
          <Dialog.Content
            className="tone3000-dialog"
            aria-describedby={undefined}
            onEscapeKeyDown={refuseDismiss}
            onPointerDownOutside={refuseDismiss}
            onInteractOutside={refuseDismiss}
          >
        <Dialog.Title className="sr-only">TONE3000 model browser</Dialog.Title>
        <header className="tone3000-dialog__header">
          <Tone3000Brand />
          <Dialog.Close asChild>
            <button className="tone3000-dialog__close" aria-label="Close TONE3000" disabled={isWorking}><X size={18} /></button>
          </Dialog.Close>
        </header>

        {phase === "intro" && (
          <div className="tone3000-intro">
            <p className="eyebrow">Models from a global community</p>
            <h2>Find a new sound without leaving Ardor</h2>
            <p>Browse TONE3000’s free library of Neural Amp Modeler captures, choose a model, and install it directly on your connected device.</p>
            <p>Ardor supports NAM A2 captures. TONE3000 will show compatible models only.</p>
            <p className="tone3000-dialog__fineprint">You’ll sign in securely in your browser. Ardor only receives access to the tone you choose.</p>
            <Button variant="primary" onClick={onContinue}>Continue to TONE3000 <ExternalLink size={15} /></Button>
          </div>
        )}

        {(phase === "waiting" || phase === "loading") && (
          <div className="tone3000-waiting">
            <LoaderCircle className="tone3000-spinner" size={28} />
            <h2>{phase === "waiting" ? "Choose a tone in your browser" : "Loading your selected tone"}</h2>
            <p>{phase === "waiting" ? "This window will update when TONE3000 sends your selection back to Ardor." : "Fetching the available NAM models and creator details…"}</p>
            {phase === "waiting" && <Button variant="quiet" onClick={onCancel}>Cancel</Button>}
          </div>
        )}

        {(phase === "detail" || phase === "installing") && selection && (
          <div className="tone3000-detail">
            {selection.tone.images?.[0]
              ? <img className="tone3000-detail__image" src={selection.tone.images[0]} alt="" loading="lazy" decoding="async" width={640} height={640} />
              : <div className="tone3000-detail__image tone3000-detail__image--empty"><Tone3000Brand compact /></div>}
            <div className="tone3000-detail__body">
              <div className="tone3000-detail__title">
                <div>
                  <p className="eyebrow">Selected tone pack</p>
                  <h2>{selection.tone.title}</h2>
                </div>
                <StatusBadge tone="info">{label(selection.tone.gear)} · NAM</StatusBadge>
              </div>
              <div className="tone3000-creator">
                {selection.tone.user.avatar_url && <img src={selection.tone.user.avatar_url} alt="" loading="lazy" decoding="async" width={28} height={28} />}
                <span>Created by <strong>@{selection.tone.user.username}</strong></span>
              </div>
              {selection.tone.description && <p className="tone3000-detail__description">{selection.tone.description}</p>}
              <label className="tone3000-model-field">
                Model
                <select value={selectedModelId ?? ""} onChange={(event) => onSelectedModelId(Number(event.target.value))} disabled={phase === "installing"}>
                  {selection.models.map((model) => <option key={model.id} value={model.id}>{model.name} · {architectureLabel(model.architecture_version)} · {label(model.size)}</option>)}
                </select>
              </label>
              <p className="tone3000-dialog__fineprint">License: {selection.tone.license.toUpperCase()} · The installed filename keeps TONE3000 and creator attribution.</p>
              <div className="tone3000-detail__actions">
                <Button variant="quiet" onClick={onCancel} disabled={phase === "installing"}>Cancel</Button>
                <Button variant="primary" onClick={onInstall} disabled={phase === "installing"}>
                  {phase === "installing" ? <><LoaderCircle className="tone3000-spinner" size={15} /> Installing…</> : <><Download size={15} /> Install on device</>}
                </Button>
              </div>
            </div>
          </div>
        )}
          </Dialog.Content>
        </PortalSurface>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
