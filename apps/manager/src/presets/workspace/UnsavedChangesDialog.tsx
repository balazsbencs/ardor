import * as Dialog from "@radix-ui/react-dialog";

import type { UnsavedChoice } from "../editor/recovery";

export function UnsavedChangesDialog({
  open,
  busy = false,
  onChoice,
}: {
  open: boolean;
  busy?: boolean;
  onChoice(choice: UnsavedChoice): void;
}) {
  return (
    <Dialog.Root open={open} onOpenChange={(nextOpen) => { if (!nextOpen && !busy) onChoice("cancel"); }}>
      <Dialog.Portal>
        <Dialog.Overlay className="dialog-overlay" />
        <Dialog.Content className="connection-dialog">
          <Dialog.Title>Unsaved changes</Dialog.Title>
          <Dialog.Description className="connection-dialog__description">
            Save this preset before leaving, discard the draft, or stay here.
          </Dialog.Description>
          <div className="connection-dialog__actions">
            <button type="button" disabled={busy} onClick={() => onChoice("cancel")}>Cancel</button>
            <button type="button" disabled={busy} onClick={() => onChoice("discard")}>Discard</button>
            <button type="button" disabled={busy} onClick={() => onChoice("save")}>{busy ? "Saving…" : "Save"}</button>
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
