import { KeyRound, ShieldAlert, SlidersHorizontal } from "lucide-react";
import { type FormEvent, useEffect, useState } from "react";

import { AppShell } from "../app/AppShell";
import { Button } from "../components/ui";
import { DeviceSessionProvider } from "../connection/deviceSession";
import { localAuthAPI, type LocalAuthStatus } from "./api";

export function LocalDeviceManager() {
  const [status, setStatus] = useState<LocalAuthStatus>();
  const [sessionGeneration, setSessionGeneration] = useState(0);
  const [setupCode, setSetupCode] = useState("");
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [confirmation, setConfirmation] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  useEffect(() => {
    localAuthAPI.status().then(setStatus).catch(() => setError("Could not reach the local Ardor service."));
  }, [sessionGeneration]);

  async function submit(event: FormEvent) {
    event.preventDefault();
    setError("");
    if (status?.state === "setup_required" && password !== confirmation) {
      setError("Passwords do not match.");
      return;
    }
    setBusy(true);
    try {
      if (status?.state === "setup_required") await localAuthAPI.setup(setupCode.trim().toUpperCase(), username, password);
      else await localAuthAPI.login(username, password);
      setPassword("");
      setConfirmation("");
      setSessionGeneration((value) => value + 1);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Local authentication failed.");
    } finally {
      setBusy(false);
    }
  }

  if (!status) return <div className="app-shell cloud-loading">
    {!error && <span className="settings-spinner" />}
    <span>{error || "Checking local access…"}</span>
    {error && <Button variant="quiet" onClick={() => { setError(""); setSessionGeneration((value) => value + 1); }}>Retry</Button>}
  </div>;
  if (status.state === "authenticated" || status.state === "disabled") {
    return <DeviceSessionProvider key={sessionGeneration}><AppShell /></DeviceSessionProvider>;
  }

  const setup = status.state === "setup_required";
  return (
    <main className="cloud-auth local-auth">
      <section className="cloud-auth__card">
        <div className="brand cloud-auth__brand"><span className="brand-mark"><SlidersHorizontal size={19} /></span><span><strong>Ardor</strong><small>Local Manager</small></span></div>
        <p className="eyebrow">{setup ? "First-time setup" : "Local access"}</p>
        <h1>{setup ? "Protect this pedal" : "Sign in to your pedal"}</h1>
        <p className="cloud-auth__intro">{setup ? "Choose credentials used only for this pedal. Use a different password from your Ardor hosted account." : "Enter the local username and password configured for this pedal."}</p>
        <div className="local-auth__warning"><ShieldAlert size={19} /><span><strong>Trusted local network only</strong><small>This direct connection uses HTTP because verified HTTPS certificates are not practical for LAN device names.</small></span></div>
        <form onSubmit={submit} className="cloud-form">
          {setup && <label>Code shown on pedal<span className="local-code-field"><KeyRound size={16} /><input required autoComplete="off" maxLength={9} placeholder="ABCD-EFGH" value={setupCode} onChange={(event) => setSetupCode(event.target.value)} /></span></label>}
          <label>Local username<input required autoComplete="username" minLength={3} maxLength={32} value={username} onChange={(event) => setUsername(event.target.value)} /></label>
          <label>{setup ? "Local password" : "Password"}<input required type="password" minLength={12} maxLength={128} autoComplete={setup ? "new-password" : "current-password"} value={password} onChange={(event) => setPassword(event.target.value)} /></label>
          {setup && <label>Confirm password<input required type="password" minLength={12} maxLength={128} autoComplete="new-password" value={confirmation} onChange={(event) => setConfirmation(event.target.value)} /></label>}
          {error && <p role="alert" className="cloud-message cloud-message--error">{error}</p>}
          <Button type="submit" variant="primary" disabled={busy}>{busy ? "Please wait…" : setup ? "Create local account" : "Sign in"}</Button>
        </form>
      </section>
    </main>
  );
}
