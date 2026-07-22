import * as Dialog from "@radix-ui/react-dialog";
import { useEffect, useRef, useState } from "react";

import { useDeviceSession } from "./deviceSession";

export function ConnectionDialog({ open, onOpenChange }: { open: boolean; onOpenChange(open: boolean): void }) {
  const session = useDeviceSession();
  const [baseUrl, setBaseUrl] = useState(session.baseUrl);
  const [token, setToken] = useState("");
  const tokenRef = useRef<HTMLInputElement>(null);
  const connectionAttempted = useRef(false);

  useEffect(() => {
    if (session.needsTokenFocus) tokenRef.current?.focus();
  }, [session.needsTokenFocus]);

  useEffect(() => {
    if (!open || !connectionAttempted.current || session.status !== "connected") return;
    connectionAttempted.current = false;
    onOpenChange(false);
  }, [open, onOpenChange, session.status]);

  const updateOpen = (nextOpen: boolean) => {
    if (!nextOpen) connectionAttempted.current = false;
    onOpenChange(nextOpen);
  };

  return (
    <Dialog.Root open={open} onOpenChange={updateOpen}>
      <Dialog.Portal>
        <Dialog.Overlay className="dialog-overlay" />
        <Dialog.Content aria-describedby={undefined} className="connection-dialog">
          <p className="eyebrow">Device connection</p>
          <Dialog.Title>Connect to Ardor</Dialog.Title>
          <form className="connection-dialog__form" onSubmit={(event) => {
            event.preventDefault();
            connectionAttempted.current = true;
            void session.connect(baseUrl, token);
          }}>
            <label>
              <span>Device URL</span>
              <input aria-label="Device URL" value={baseUrl} onChange={(event) => setBaseUrl(event.target.value)} />
            </label>
            <label>
              <span>Bearer token</span>
              <input ref={tokenRef} aria-label="Bearer token" type="password" value={token} onChange={(event) => setToken(event.target.value)} />
            </label>
            {session.error && <div role="alert">{session.error.message}</div>}
            <div className="connection-dialog__actions">
              <Dialog.Close type="button">Cancel</Dialog.Close>
              <button type="submit" disabled={session.status === "connecting"}>
                {session.status === "connecting" ? "Connecting…" : "Connect"}
              </button>
            </div>
          </form>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
