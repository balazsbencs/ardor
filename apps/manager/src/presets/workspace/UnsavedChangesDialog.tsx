import * as Dialog from "@radix-ui/react-dialog";

import "../../app/dialogs.css";
import { Button } from "../../components/ui";
import { PortalSurface } from "../../theme/surface";
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
        <PortalSurface>
        <Dialog.Overlay className="dlg-scrim" />
        <Dialog.Content className="dlg">
          <Dialog.Title className="dlg__title">Unsaved changes</Dialog.Title>
          <Dialog.Description className="dlg__text">
            Save this preset before you leave. You can also discard the changes or stay here.
          </Dialog.Description>
          <div className="dlg__actions">
            <Button type="button" variant="quiet" disabled={busy} onClick={() => onChoice("cancel")}>Cancel</Button>
            <Button type="button" variant="danger" disabled={busy} onClick={() => onChoice("discard")}>Discard</Button>
            <Button type="button" variant="primary" disabled={busy} onClick={() => onChoice("save")}>{busy ? "Saving…" : "Save"}</Button>
          </div>
        </Dialog.Content>
        </PortalSurface>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
