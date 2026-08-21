import { Cable, KeyRound, LogOut, RefreshCw, ShieldCheck, SlidersHorizontal, Unplug, UserRound } from "lucide-react";
import { type FormEvent, useCallback, useEffect, useState } from "react";

import { Button, StatusBadge } from "../components/ui";
import { AppShell } from "../app/AppShell";
import { DeviceSessionProvider } from "../connection/deviceSession";
import { CloudTransport } from "./CloudTransport";
import { type Account, type Claim, CloudAPIError, type Device, cloudAPI } from "./api";

type AuthView = "login" | "register" | "recover";

function messageFor(error: unknown): string {
  return error instanceof CloudAPIError ? error.message : "The service could not be reached.";
}

function AuthPanel({ onAuthenticated, onRegistered }: {
  onAuthenticated(account: Account): void;
  onRegistered(account: Account, recoveryCodes: string[]): void;
}) {
  const [view, setView] = useState<AuthView>("login");
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [recoveryCode, setRecoveryCode] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  async function submit(event: FormEvent) {
    event.preventDefault();
    setBusy(true);
    setError("");
    try {
      if (view === "register") {
        const result = await cloudAPI.register(username, password);
        onRegistered(result.account, result.recoveryCodes);
      } else if (view === "recover") {
        const result = await cloudAPI.recover(username, recoveryCode, password);
        onAuthenticated(result.account);
      } else {
        const result = await cloudAPI.login(username, password);
        onAuthenticated(result.account);
      }
    } catch (caught) {
      setError(messageFor(caught));
    } finally {
      setBusy(false);
    }
  }

  return (
    <main className="cloud-auth">
      <section className="cloud-auth__card">
        <div className="brand cloud-auth__brand"><span className="brand-mark"><SlidersHorizontal size={19} /></span><span><strong>Ardor</strong><small>Cloud Manager</small></span></div>
        <p className="eyebrow">{view === "register" ? "Create account" : view === "recover" ? "Account recovery" : "Welcome back"}</p>
        <h1>{view === "register" ? "Set up your manager" : view === "recover" ? "Use a recovery code" : "Manage your pedals"}</h1>
        <p className="cloud-auth__intro">Your browser connects to Ardor over HTTPS; pedals keep their own outbound encrypted connection.</p>
        <form onSubmit={submit} className="cloud-form">
          <label>Username<input required autoComplete="username" value={username} onChange={(event) => setUsername(event.target.value)} /></label>
          {view === "recover" && <label>Recovery code<input required autoComplete="off" value={recoveryCode} onChange={(event) => setRecoveryCode(event.target.value)} /></label>}
          <label>{view === "recover" ? "New password" : "Password"}<input required minLength={12} maxLength={128} type="password" autoComplete={view === "login" ? "current-password" : "new-password"} value={password} onChange={(event) => setPassword(event.target.value)} /></label>
          {error && <p role="alert" className="cloud-message cloud-message--error">{error}</p>}
          <Button type="submit" variant="primary" disabled={busy}>{busy ? "Please wait…" : view === "register" ? "Create account" : view === "recover" ? "Reset password" : "Sign in"}</Button>
        </form>
        <div className="cloud-auth__switch">
          {view !== "login" && <button onClick={() => { setView("login"); setError(""); }}>Sign in</button>}
          {view !== "register" && <button onClick={() => { setView("register"); setError(""); }}>Create account</button>}
          {view !== "recover" && <button onClick={() => { setView("recover"); setError(""); }}>Recover account</button>}
        </div>
      </section>
    </main>
  );
}

function RecoveryCodes({ codes, onDone }: { codes: string[]; onDone(): void }) {
  const [acknowledged, setAcknowledged] = useState(false);
  return (
    <main className="cloud-auth">
      <section className="cloud-auth__card cloud-recovery">
        <ShieldCheck size={30} />
        <p className="eyebrow">One-time display</p>
        <h1>Save your recovery codes</h1>
        <p className="cloud-auth__intro">Each code works once. Ardor stores only hashes and cannot show these again.</p>
        <pre>{codes.join("\n")}</pre>
        <Button onClick={() => navigator.clipboard?.writeText(codes.join("\n"))}>Copy codes</Button>
        <label className="cloud-check"><input type="checkbox" checked={acknowledged} onChange={(event) => setAcknowledged(event.target.checked)} /> I saved these codes somewhere safe.</label>
        <Button variant="primary" disabled={!acknowledged} onClick={onDone}>Continue</Button>
      </section>
    </main>
  );
}

function DeviceDashboard({ account, onSignedOut, onManage }: { account: Account; onSignedOut(): void; onManage(device: Device): void }) {
  const [devices, setDevices] = useState<Device[]>([]);
  const [claimCode, setClaimCode] = useState("");
  const [claim, setClaim] = useState<Claim | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  const refresh = useCallback(async () => {
    try {
      const result = await cloudAPI.devices();
      setDevices(result.devices);
    } catch (caught) {
      setError(messageFor(caught));
    }
  }, []);

  useEffect(() => {
    void refresh();
    const timer = window.setInterval(() => void refresh(), 10_000);
    return () => window.clearInterval(timer);
  }, [refresh]);

  useEffect(() => {
    if (!claim || claim.status !== "confirm_on_device") return;
    const timer = window.setInterval(async () => {
      try {
        const current = await cloudAPI.claim(claim.id);
        setClaim(current);
        if (current.status === "claimed") void refresh();
      } catch (caught) {
        setError(messageFor(caught));
      }
    }, 1_500);
    return () => window.clearInterval(timer);
  }, [claim, refresh]);

  async function beginClaim(event: FormEvent) {
    event.preventDefault();
    setBusy(true);
    setError("");
    try {
      setClaim(await cloudAPI.beginClaim(claimCode.trim().toUpperCase()));
      setClaimCode("");
    } catch (caught) {
      setError(messageFor(caught));
    } finally {
      setBusy(false);
    }
  }

  async function unclaim(device: Device) {
    if (!window.confirm(`Remove ${device.id} from your account? The pedal can be claimed again afterwards.`)) return;
    setBusy(true);
    setError("");
    try {
      await cloudAPI.unclaim(device.id);
      await refresh();
    } catch (caught) {
      setError(messageFor(caught));
    } finally {
      setBusy(false);
    }
  }

  async function signOut(everywhere: boolean) {
    setBusy(true);
    try {
      await (everywhere ? cloudAPI.logoutAll() : cloudAPI.logout());
      onSignedOut();
    } catch (caught) {
      setError(messageFor(caught));
      setBusy(false);
    }
  }

  return (
    <div className="cloud-dashboard">
      <header className="app-topbar">
        <div className="brand"><span className="brand-mark"><SlidersHorizontal size={19} /></span><span><strong>Ardor</strong><small>Cloud Manager</small></span></div>
        <div className="cloud-account"><UserRound size={15} /><strong>{account.username}</strong><Button variant="quiet" disabled={busy} onClick={() => void signOut(false)}><LogOut size={15} /> Sign out</Button></div>
      </header>
      <main className="cloud-main">
        <section className="cloud-heading"><div><p className="eyebrow">Your equipment</p><h1>Devices</h1><p>Claim a pedal once, then manage it from any signed-in browser.</p></div><Button onClick={() => void refresh()}><RefreshCw size={15} /> Refresh</Button></section>
        {error && <p role="alert" className="cloud-message cloud-message--error">{error}</p>}
        <section className="cloud-claim-card">
          <div><KeyRound size={21} /><div><h2>Claim a pedal</h2><p>Enter the code displayed on the pedal, then approve the account name physically on the device.</p></div></div>
          <form onSubmit={beginClaim}><input aria-label="Claim code" required maxLength={12} placeholder="ABCD-EFGH" value={claimCode} onChange={(event) => setClaimCode(event.target.value)} /><Button type="submit" variant="primary" disabled={busy}>Continue</Button></form>
          {claim && <p className={`cloud-message cloud-message--${claim.status === "claimed" ? "success" : claim.status === "confirm_on_device" ? "info" : "error"}`}>{claim.status === "confirm_on_device" ? "Waiting for approval on the pedal…" : claim.status === "claimed" ? "Pedal claimed successfully." : "The claim was not approved."}</p>}
        </section>
        <section className="cloud-device-list" aria-label="Claimed devices">
          {devices.length === 0 ? <div className="cloud-empty"><Cable size={30} /><h2>No claimed devices</h2><p>Connect a pedal to the internet and use the code shown on its display.</p></div> : devices.map((device) => (
            <article className="cloud-device" key={device.id}>
              <span className={`cloud-device__icon ${device.online ? "is-online" : ""}`}><Cable size={20} /></span>
              <div><strong>Ardor Pedal</strong><code>{device.id}</code></div>
              <StatusBadge tone={device.online ? "success" : "neutral"}>{device.online ? "Online" : "Offline"}</StatusBadge>
              <small>{device.lastSeenAt ? `Last seen ${new Date(device.lastSeenAt).toLocaleString()}` : "Not connected yet"}</small>
              <Button variant="primary" disabled={!device.online} onClick={() => onManage(device)}>Manage presets</Button>
              <Button variant="quiet" disabled={busy} onClick={() => void unclaim(device)}><Unplug size={14} /> Unclaim</Button>
            </article>
          ))}
        </section>
        <section className="cloud-security"><div><ShieldCheck size={20} /><span><strong>Account security</strong><small>Revoke every active browser session if you lose access to a device.</small></span></div><Button variant="danger" disabled={busy} onClick={() => void signOut(true)}>Sign out everywhere</Button></section>
      </main>
    </div>
  );
}

export function HostedManager() {
  const [account, setAccount] = useState<Account | null>(null);
  const [checking, setChecking] = useState(true);
  const [recoveryCodes, setRecoveryCodes] = useState<string[] | null>(null);
  const [selectedDevice, setSelectedDevice] = useState<Device | null>(null);

  useEffect(() => {
    cloudAPI.me().then(setAccount).catch(() => undefined).finally(() => setChecking(false));
  }, []);

  if (checking) return <div className="app-shell cloud-loading"><span className="settings-spinner" /><span>Loading Ardor Manager…</span></div>;
  if (account && selectedDevice) {
    return <DeviceSessionProvider autoConnect connectionId={`cloud:${selectedDevice.id}`} clientFactory={() => new CloudTransport(selectedDevice.id, selectedDevice.remoteMutationsEnabled)}><AppShell onCloudDevices={() => setSelectedDevice(null)} tone3000DeviceId={selectedDevice.id} /></DeviceSessionProvider>;
  }
  return (
    <div className="app-shell" data-palette="slate">
      {account && recoveryCodes ? <RecoveryCodes codes={recoveryCodes} onDone={() => setRecoveryCodes(null)} />
        : account ? <DeviceDashboard account={account} onSignedOut={() => setAccount(null)} onManage={setSelectedDevice} />
          : <AuthPanel onAuthenticated={setAccount} onRegistered={(created, codes) => { setAccount(created); setRecoveryCodes(codes); }} />}
    </div>
  );
}
