import * as Dialog from "@radix-ui/react-dialog";

import { PortalSurface } from "../theme/surface";
import { useEffect, useRef, useState } from "react";

import { useDeviceSession } from "./deviceSession";
import { isDeviceHostedRuntime, isHostedCloudRuntime } from "../runtime/platform";
import { localAuthAPI } from "../localAuth/api";

export function ConnectionDialog({ open, onOpenChange }: { open: boolean; onOpenChange(open: boolean): void }) {
  const session = useDeviceSession();
  const deviceHosted = isDeviceHostedRuntime();
  const hostedCloud = isHostedCloudRuntime();
  const managedRuntime = deviceHosted || hostedCloud;
  const [baseUrl, setBaseUrl] = useState(session.baseUrl);
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [authError, setAuthError] = useState("");
  const connectionAttempted = useRef(false);

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
        <PortalSurface>
        <Dialog.Overlay className="dialog-overlay" />
        <Dialog.Content aria-describedby={undefined} className="connection-dialog">
          <p className="eyebrow">Device connection</p>
          <Dialog.Title>Connect to Ardor</Dialog.Title>
          <form className="connection-dialog__form" onSubmit={(event) => {
            event.preventDefault();
            connectionAttempted.current = true;
            setAuthError("");
            const connect = async () => {
              const target = managedRuntime ? session.baseUrl : baseUrl;
              if (managedRuntime) {
                await session.connect(target);
                return;
              }
              const status = await localAuthAPI.status(target);
              if (status.state === "setup_required") throw new Error("Open the pedal address in a browser and use the code shown on its display for first-time setup.");
              if (status.state === "disabled") {
                await session.connect(target);
                return;
              }
              const result = await localAuthAPI.login(username, password, target);
              await session.connect(target, result.sessionToken);
            };
            void connect().catch((reason: unknown) => {
              connectionAttempted.current = false;
              setAuthError(reason instanceof Error ? reason.message : "Could not sign in to the pedal.");
            });
          }}>
            {managedRuntime
              ? <p>{hostedCloud ? "This pedal is connected through Ardor Cloud" : "This manager is hosted by the Ardor device"}: <strong>{session.device?.deviceName ?? "Ardor Pedal"}</strong>.</p>
              : <label>
                  <span>Device URL</span>
                  <input aria-label="Device URL" value={baseUrl} onChange={(event) => setBaseUrl(event.target.value)} />
                </label>}
            {!managedRuntime && <label>
              <span>Local username</span>
              <input aria-label="Local username" autoComplete="username" value={username} onChange={(event) => setUsername(event.target.value)} />
            </label>}
            {!managedRuntime && <label>
              <span>Local password</span>
              <input aria-label="Local password" autoComplete="current-password" type="password" value={password} onChange={(event) => setPassword(event.target.value)} />
            </label>}
            {(authError || session.error) && <div role="alert">{authError || session.error?.message}</div>}
            <div className="connection-dialog__actions">
              <Dialog.Close type="button">Cancel</Dialog.Close>
              <button type="submit" disabled={session.status === "connecting"}>
                {session.status === "connecting" ? "Connecting…" : managedRuntime ? "Retry connection" : "Connect"}
              </button>
            </div>
          </form>
        </Dialog.Content>
        </PortalSurface>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
