import { Download, ExternalLink, LoaderCircle, X } from "lucide-react";

import { Button, StatusBadge } from "../components/ui";
import type { Tone3000Selection } from "./client";
import tone3000Logo from "./tone3000-logo.svg";

export type Tone3000Phase = "idle" | "intro" | "waiting" | "loading" | "detail" | "installing";

function label(value: string): string {
  return value.split("-").map((part) => part.charAt(0).toUpperCase() + part.slice(1)).join(" + ");
}

function architecture(value: "1" | "2" | "custom" | null): string {
  if (value === "1") return "A1";
  if (value === "2") return "A2";
  if (value === "custom") return "Custom";
  return "NAM";
}

export function Tone3000Brand({ compact = false }: { compact?: boolean }) {
  return (
    <span className={compact ? "tone3000-brand tone3000-brand--compact" : "tone3000-brand"}>
      <img src={tone3000Logo} alt="TONE3000" />
      {!compact && <small>NAM Captures and IRs</small>}
    </span>
  );
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
  if (phase === "idle") return null;
  const isWorking = phase === "loading" || phase === "installing";

  return (
    <div className="tone3000-overlay" role="presentation">
      <section className="tone3000-dialog" role="dialog" aria-modal="true" aria-label="TONE3000 model browser">
        <header className="tone3000-dialog__header">
          <Tone3000Brand />
          <button className="tone3000-dialog__close" aria-label="Close TONE3000" onClick={onCancel} disabled={isWorking}><X size={18} /></button>
        </header>

        {phase === "intro" && (
          <div className="tone3000-intro">
            <p className="eyebrow">Models from a global community</p>
            <h2>Find a new sound without leaving Ardor</h2>
            <p>Browse TONE3000’s free library of Neural Amp Modeler captures, choose a model, and install it directly on your connected device.</p>
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
              ? <img className="tone3000-detail__image" src={selection.tone.images[0]} alt="" />
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
                {selection.tone.user.avatar_url && <img src={selection.tone.user.avatar_url} alt="" />}
                <span>Created by <strong>@{selection.tone.user.username}</strong></span>
              </div>
              {selection.tone.description && <p className="tone3000-detail__description">{selection.tone.description}</p>}
              <label className="tone3000-model-field">
                Model
                <select value={selectedModelId ?? ""} onChange={(event) => onSelectedModelId(Number(event.target.value))} disabled={phase === "installing"}>
                  {selection.models.map((model) => <option key={model.id} value={model.id}>{model.name} · {architecture(model.architecture_version)} · {label(model.size)}</option>)}
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
      </section>
    </div>
  );
}
