import { flushSync } from "react-dom";

type TransitionDocument = Document & { startViewTransition?: (update: () => void) => unknown };

/** Runs a state update inside a view transition, so the chain folds into the chip strip like the pedal. */
export function withViewTransition(update: () => void): void {
  const doc = document as TransitionDocument;
  if (!doc.startViewTransition || matchMedia("(prefers-reduced-motion: reduce)").matches) {
    update();
    return;
  }
  doc.startViewTransition(() => flushSync(update));
}
