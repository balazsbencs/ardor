import * as Dialog from "@radix-ui/react-dialog";
import type { ReactNode } from "react";

import { PortalSurface } from "../theme/surface";

/**
 * A corner confirmation panel. It is a real modal — Radix traps focus, restores it on close,
 * and closes on Escape — so the modal semantics it announces are the semantics it has.
 */
export function ConfirmPopover({
  open,
  onOpenChange,
  badge,
  title,
  children,
  actions,
  role = "dialog",
}: {
  open: boolean;
  onOpenChange(open: boolean): void;
  badge: ReactNode;
  title: ReactNode;
  children: ReactNode;
  actions: ReactNode;
  role?: "dialog" | "alertdialog";
}) {
  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal>
        <PortalSurface>
          <Dialog.Content className="confirm-popover" role={role} aria-describedby={undefined}>
            <div>{badge}<Dialog.Title>{title}</Dialog.Title>{children}</div>
            <div>{actions}</div>
          </Dialog.Content>
        </PortalSurface>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
