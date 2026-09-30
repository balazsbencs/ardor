import * as Dialog from "@radix-ui/react-dialog";

import { useEffect, useRef, useState } from "react";

import "../app/dialogs.css";
import { Button } from "../components/ui";
import { localAuthAPI } from "../localAuth/api";
import { isDeviceHostedRuntime, isHostedCloudRuntime } from "../runtime/platform";
import { PortalSurface } from "../theme/surface";
import { useDeviceSession } from "./deviceSession";
import { loginErrorMessage } from "./loginError";

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
        <Dialog.Overlay className="dlg-scrim" />
        <Dialog.Content aria-describedby={undefined} className="dlg">
          <Dialog.Title className="dlg__title">Connect to Ardor</Dialog.Title>
          <form className="dlg__form" onSubmit={(event) => {
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
              if (status.state === "setup_required") throw new Error("Open the pedal address in a browser. Enter the code from the pedal display to set up the pedal.");
              if (status.state === "disabled") {
                await session.connect(target);
                return;
              }
              const result = await localAuthAPI.login(username, password, target);
              await session.connect(target, result.sessionToken);
            };
            void connect().catch((reason: unknown) => {
              connectionAttempted.current = false;
              setAuthError(loginErrorMessage(reason));
            });
          }}>
            {managedRuntime
              ? <p className="dlg__text">{hostedCloud ? "Ardor Cloud connects this pedal" : "This manager runs on the pedal"}: <strong>{session.device?.deviceName ?? "Ardor Pedal"}</strong>.</p>
              : <label className="dlg__field">
                  <span className="dlg__label">Device URL</span>
                  <input aria-label="Device URL" value={baseUrl} onChange={(event) => setBaseUrl(event.target.value)} />
                </label>}
            {!managedRuntime && <label className="dlg__field">
              <span className="dlg__label">Local username</span>
              <input aria-label="Local username" autoComplete="username" value={username} onChange={(event) => setUsername(event.target.value)} />
            </label>}
            {!managedRuntime && <label className="dlg__field">
              <span className="dlg__label">Local password</span>
              <input aria-label="Local password" autoComplete="current-password" type="password" value={password} onChange={(event) => setPassword(event.target.value)} />
            </label>}
            {(authError || session.error) && <div role="alert" className="dlg__error">{authError || session.error?.message}</div>}
            <div className="dlg__actions">
              <Dialog.Close asChild><Button type="button" variant="quiet">Cancel</Button></Dialog.Close>
              <Button type="submit" variant="primary" disabled={session.status === "connecting"}>
                {session.status === "connecting" ? "Connecting…" : managedRuntime ? "Retry connection" : "Connect"}
              </Button>
            </div>
          </form>
        </Dialog.Content>
        </PortalSurface>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
