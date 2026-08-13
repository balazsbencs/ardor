import * as Dialog from "@radix-ui/react-dialog";
import {
  Check,
  Download,
  Eye,
  EyeOff,
  LogOut,
  Palette,
  RotateCcw,
  RefreshCw,
  Settings,
  ShieldAlert,
  Trash2,
  Wifi,
  X,
} from "lucide-react";
import { useEffect, useState } from "react";

import type { UpdateStatus, WiFiSettings } from "../api/types";
import { Button, IconButton, StatusBadge, cx } from "../components/ui";
import { useDeviceSession } from "../connection/deviceSession";
import { localAuthAPI } from "../localAuth/api";
import { isDeviceHostedRuntime } from "../runtime/platform";
import { accentChoices, accentVariables, defaultAccent, type Theme } from "../theme/accent";

type SettingsSection = "appearance" | "wifi" | "updates" | "security";

function wifiTone(status?: string): "neutral" | "success" | "warning" {
  if (status === "connected") return "success";
  if (status === "connecting" || status === "restarting") return "warning";
  return "neutral";
}

function updateTone(status?: string): "neutral" | "success" | "warning" | "danger" {
  if (status === "succeeded") return "success";
  if (status === "failed" || status === "rolled_back") return "danger";
  if (status && !["idle", "available"].includes(status)) return "warning";
  return "neutral";
}

function formatBytes(bytes: number): string {
  return new Intl.NumberFormat(undefined, { style: "unit", unit: "megabyte", maximumFractionDigits: 1 })
    .format(bytes / (1024 * 1024));
}

export function SettingsDialog({
  open,
  onOpenChange,
  accent,
  theme,
  onAccentChange,
}: {
  open: boolean;
  onOpenChange(open: boolean): void;
  accent: string;
  theme: Theme;
  onAccentChange(accent: string): void;
}) {
  const session = useDeviceSession();
  const [section, setSection] = useState<SettingsSection>("appearance");
  const [wifi, setWifi] = useState<WiFiSettings>();
  const [ssid, setSSID] = useState("");
  const [country, setCountry] = useState("HU");
  const [password, setPassword] = useState("");
  const [showPassword, setShowPassword] = useState(false);
  const [loading, setLoading] = useState(false);
  const [saving, setSaving] = useState(false);
  const [notice, setNotice] = useState<string>();
  const [error, setError] = useState<string>();
  const [securityBusy, setSecurityBusy] = useState(false);
  const [factoryNotice, setFactoryNotice] = useState<string>();
  const [updateStatus, setUpdateStatus] = useState<UpdateStatus>();
  const [updateBusy, setUpdateBusy] = useState(false);
  const [updateError, setUpdateError] = useState<string>();
  const localDevice = isDeviceHostedRuntime();

  const wifiAvailable = session.status === "connected"
    && Boolean(session.client)
    && session.device?.capabilities.wifiSettings === true;
  const updateAvailable = session.status === "connected"
    && Boolean(session.client)
    && session.device?.capabilities.softwareUpdate === true;
  const portalStyle = accentVariables(accent, theme);

  useEffect(() => {
    if (!open || section !== "wifi" || !wifiAvailable || !session.client) return;
    let cancelled = false;
    setLoading(true);
    setError(undefined);
    void session.client.getWiFiSettings()
      .then((settings) => {
        if (cancelled) return;
        setWifi(settings);
        setSSID(settings.ssid ?? "");
        setCountry(settings.country || "HU");
        setPassword("");
      })
      .catch((reason: unknown) => {
        if (!cancelled) setError(reason instanceof Error ? reason.message : "Could not load Wi-Fi settings.");
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => { cancelled = true; };
  }, [open, section, session.client, wifiAvailable]);

  useEffect(() => {
    if (!open || section !== "updates" || !updateAvailable || !session.client) return;
    let cancelled = false;
    setUpdateBusy(true);
    setUpdateError(undefined);
    void session.client.getUpdateStatus()
      .then((status) => { if (!cancelled) setUpdateStatus(status); })
      .catch((reason: unknown) => {
        if (!cancelled) setUpdateError(reason instanceof Error ? reason.message : "Could not read update status.");
      })
      .finally(() => { if (!cancelled) setUpdateBusy(false); });
    return () => { cancelled = true; };
  }, [open, section, session.client, updateAvailable]);

  const saveWiFi = async () => {
    if (!session.client || saving) return;
    setSaving(true);
    setNotice(undefined);
    setError(undefined);
    try {
      const updated = await session.client.updateWiFiSettings({
        ssid: ssid.trim(),
        country: country.trim().toUpperCase(),
        ...(password ? { password } : {}),
      });
      setWifi(updated);
      setPassword("");
      setNotice("Wi-Fi saved. The pedal is reconnecting; this window may briefly lose contact.");
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not save Wi-Fi settings.");
    } finally {
      setSaving(false);
    }
  };

  const logoutLocal = async () => {
    setSecurityBusy(true);
    try {
      await localAuthAPI.logout();
      window.location.reload();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not sign out.");
      setSecurityBusy(false);
    }
  };

  const resetLocal = async () => {
    if (!window.confirm("Reset local access? This removes the local username, password, and every local session. Presets, assets, Wi-Fi, and cloud ownership are preserved.")) return;
    setSecurityBusy(true);
    try {
      await localAuthAPI.resetLocalAccess();
      window.location.reload();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not reset local access.");
      setSecurityBusy(false);
    }
  };

  const beginFactoryReset = async () => {
    if (!window.confirm("Request a factory reset? You must approve it physically on the pedal. It will remove all presets, models, IRs, Wi-Fi, settings, and local access.")) return;
    setSecurityBusy(true);
    setError(undefined);
    try {
      const reset = await localAuthAPI.beginFactoryReset();
      setFactoryNotice(`Waiting for physical confirmation on the pedal. Request ${reset.resetId.slice(0, 8)} expires in two minutes.`);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not request a factory reset.");
    } finally {
      setSecurityBusy(false);
    }
  };

  const checkForUpdate = async () => {
    if (!session.client || updateBusy) return;
    setUpdateBusy(true);
    setUpdateError(undefined);
    try {
      setUpdateStatus(await session.client.checkForUpdate());
    } catch (reason) {
      setUpdateError(reason instanceof Error ? reason.message : "Could not check GitHub Releases.");
    } finally {
      setUpdateBusy(false);
    }
  };

  const installUpdate = async () => {
    const available = updateStatus?.available;
    if (!session.client || !available || updateBusy || available.reflashRequired) return;
    if (!window.confirm(`Install Ardor ${available.version}? Audio will mute and Manager will disconnect briefly while the pedal applications restart.`)) return;
    setUpdateBusy(true);
    setUpdateError(undefined);
    try {
      setUpdateStatus(await session.client.installUpdate(available.version));
      const deadline = Date.now() + 2 * 60_000;
      while (Date.now() < deadline) {
        await new Promise((resolve) => window.setTimeout(resolve, 2_000));
        try {
          const status = await session.client.getUpdateStatus();
          setUpdateStatus(status);
          if (status.state === "succeeded") {
            window.location.reload();
            return;
          }
          if (status.state === "failed" || status.state === "rolled_back") return;
        } catch {
          // A short connection loss is expected while managerd is replaced.
        }
      }
      setUpdateError("The pedal did not reconnect within two minutes. It may still be completing recovery; reconnect and open Updates again.");
    } catch (reason) {
      setUpdateError(reason instanceof Error ? reason.message : "Could not start the update.");
    } finally {
      setUpdateBusy(false);
    }
  };

  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal>
        <div className="app-shell portal-surface" data-theme={theme} style={portalStyle}>
          <Dialog.Overlay className="dialog-overlay" />
          <Dialog.Content className="settings-dialog" aria-describedby="settings-description">
          <header className="settings-dialog__header">
            <span className="settings-dialog__mark"><Settings size={18} /></span>
            <div>
              <Dialog.Title>Settings</Dialog.Title>
              <Dialog.Description id="settings-description">
                Personalize Ardor Manager and configure the connected pedal.
              </Dialog.Description>
            </div>
            <Dialog.Close asChild><IconButton label="Close settings"><X size={17} /></IconButton></Dialog.Close>
          </header>

          <div className="settings-dialog__body">
            <nav className="settings-nav" aria-label="Settings sections">
              <button className={cx(section === "appearance" && "is-active")} onClick={() => setSection("appearance")}>
                <Palette size={17} /><span>Appearance</span>
              </button>
              <button className={cx(section === "wifi" && "is-active")} onClick={() => setSection("wifi")}>
                <Wifi size={17} /><span>Wi-Fi</span>
              </button>
              {session.device && <button className={cx(section === "updates" && "is-active")} onClick={() => setSection("updates")}>
                <Download size={17} /><span>Updates</span>
              </button>}
              {localDevice && <button className={cx(section === "security" && "is-active")} onClick={() => setSection("security")}>
                <ShieldAlert size={17} /><span>Security</span>
              </button>}
            </nav>

            {section === "appearance" ? (
              <section className="settings-panel" aria-labelledby="appearance-heading">
                <div className="settings-panel__heading">
                  <p className="eyebrow">Manager appearance</p>
                  <h2 id="appearance-heading">Accent color</h2>
                  <p>Used for primary actions, selection states, and focus indicators throughout the app.</p>
                </div>
                <div className="accent-setting">
                  <div className="accent-preview" style={{ backgroundColor: accent }}>
                    <span style={{ color: accent }}><Check size={16} /></span>
                    <div>
                      <strong>Live preview</strong>
                      <small>Changes apply everywhere immediately.</small>
                    </div>
                  </div>
                  <div className="accent-swatches" role="group" aria-label="Accent color presets">
                    {accentChoices.map((choice) => (
                      <button
                        key={choice.value}
                        className={cx(accent.toLowerCase() === choice.value && "is-active")}
                        style={{ backgroundColor: choice.value }}
                        aria-label={choice.name}
                        aria-pressed={accent.toLowerCase() === choice.value}
                        onClick={() => onAccentChange(choice.value)}
                      >
                        {accent.toLowerCase() === choice.value && <Check size={14} />}
                      </button>
                    ))}
                  </div>
                  <label className="color-field">
                    <span>Custom color</span>
                    <span className="color-field__control">
                      <input
                        aria-label="Custom accent color"
                        type="color"
                        value={accent}
                        onChange={(event) => onAccentChange(event.target.value)}
                      />
                      <output>{accent.toUpperCase()}</output>
                    </span>
                  </label>
                  <Button variant="quiet" onClick={() => onAccentChange(defaultAccent)}>
                    <RotateCcw size={15} /> Reset to Ardor green
                  </Button>
                </div>
              </section>
            ) : section === "wifi" ? (
              <section className="settings-panel" aria-labelledby="wifi-heading">
                <div className="settings-panel__heading settings-panel__heading--with-status">
                  <div>
                    <p className="eyebrow">Connected pedal</p>
                    <h2 id="wifi-heading">Wi-Fi</h2>
                    <p>Credentials are stored on the pedal’s writable data partition, never in the system image.</p>
                  </div>
                  {wifi && <StatusBadge tone={wifiTone(wifi.status)}>{wifi.status}</StatusBadge>}
                </div>

                {!wifiAvailable ? (
                  <div className="settings-empty">
                    <Wifi size={24} />
                    <strong>Connect to a pedal first</strong>
                    <p>Wi-Fi settings are sent directly to the device. Use Ethernet or the current Wi-Fi connection for initial setup.</p>
                  </div>
                ) : loading ? (
                  <div className="settings-empty"><span className="settings-spinner" /><strong>Reading pedal settings…</strong></div>
                ) : (
                  <form className="wifi-form" onSubmit={(event) => { event.preventDefault(); void saveWiFi(); }}>
                    <label>
                      <span>Network name</span>
                      <input
                        autoComplete="off"
                        maxLength={32}
                        required
                        value={ssid}
                        onChange={(event) => setSSID(event.target.value)}
                        placeholder="Wi-Fi network (SSID)"
                      />
                    </label>
                    <label>
                      <span>Password</span>
                      <span className="password-field">
                        <input
                          autoComplete="new-password"
                          minLength={8}
                          type={showPassword ? "text" : "password"}
                          value={password}
                          onChange={(event) => setPassword(event.target.value)}
                          placeholder={wifi?.configured ? "Leave blank to keep current password" : "8 characters minimum"}
                          required={!wifi?.configured}
                        />
                        <IconButton
                          type="button"
                          label={showPassword ? "Hide password" : "Show password"}
                          onClick={() => setShowPassword((current) => !current)}
                        >
                          {showPassword ? <EyeOff size={16} /> : <Eye size={16} />}
                        </IconButton>
                      </span>
                    </label>
                    <label className="wifi-country">
                      <span>Country code</span>
                      <input
                        aria-describedby="country-help"
                        inputMode="text"
                        maxLength={2}
                        minLength={2}
                        pattern="[A-Za-z]{2}"
                        required
                        value={country}
                        onChange={(event) => setCountry(event.target.value.toUpperCase())}
                      />
                      <small id="country-help">Two-letter code used for legal radio channels, for example HU, DE, or US.</small>
                    </label>
                    {wifi?.ipAddress && <p className="wifi-address">Pedal address <strong>{wifi.ipAddress}</strong></p>}
                    {error && <div className="settings-message settings-message--error" role="alert">{error}</div>}
                    {notice && <div className="settings-message settings-message--success" role="status">{notice}</div>}
                    <div className="settings-panel__actions">
                      <Button type="submit" variant="primary" disabled={saving}>
                        <Wifi size={16} />{saving ? "Saving…" : wifi?.configured ? "Save & reconnect" : "Connect pedal"}
                      </Button>
                    </div>
                  </form>
                )}
              </section>
            ) : section === "updates" ? (
              <section className="settings-panel" aria-labelledby="updates-heading">
                <div className="settings-panel__heading settings-panel__heading--with-status">
                  <div>
                    <h2 id="updates-heading">Device software</h2>
                    <p>Install signed Ardor application releases from GitHub. Presets, models, IRs, Wi-Fi, and local access stay on the pedal.</p>
                  </div>
                  {updateStatus && <StatusBadge tone={updateTone(updateStatus.state)}>{updateStatus.state.replace(/_/g, " ")}</StatusBadge>}
                </div>
                {!updateAvailable ? (
                  <div className="settings-empty">
                    <Download size={24} />
                    <strong>Updates require a bootstrap image</strong>
                    <p>This pedal does not advertise signed OTA support. Flash an OTA-capable Ardor image before installing releases here.</p>
                  </div>
                ) : updateBusy && !updateStatus ? (
                  <div className="settings-empty"><span className="settings-spinner" /><strong>Reading device software…</strong></div>
                ) : (
                  <div className="update-overview">
                    <dl className="update-versions">
                      <div><dt>Installed</dt><dd>{updateStatus?.installedVersion ?? session.device?.softwareVersion ?? "Unknown"}</dd></div>
                      <div><dt>Base image</dt><dd>{updateStatus?.baseVersion ?? session.device?.baseSystemVersion ?? "Unknown"}</dd></div>
                    </dl>
                    {updateStatus?.available ? (
                      <div className="update-release">
                        <div>
                          <strong>Ardor {updateStatus.available.version}</strong>
                          <span>{formatBytes(updateStatus.available.bundleSize)}</span>
                        </div>
                        {updateStatus.available.reflashRequired ? (
                          <p>{updateStatus.available.incompatibility ?? "This release changes the base system and must be flashed to the SD card."}</p>
                        ) : (
                          <p>The signed application bundle is compatible with this pedal and ready to install.</p>
                        )}
                        <a href={updateStatus.available.releaseUrl} target="_blank" rel="noreferrer">Read release notes</a>
                      </div>
                    ) : updateStatus?.checkedAt ? (
                      <p className="update-current"><Check size={16} />This pedal already has the newest compatible application release.</p>
                    ) : (
                      <p className="update-current">Check GitHub Releases when you are ready to update. Nothing installs automatically.</p>
                    )}
                    {updateStatus && ["downloading", "verifying", "staged", "restarting", "validating"].includes(updateStatus.state) && (
                      <div className="update-progress" role="status"><span className="settings-spinner" /><div><strong>Update in progress</strong><small>Keep the pedal powered. Manager may disconnect during restart.</small></div></div>
                    )}
                    {(updateError || updateStatus?.errorMessage) && <div className="settings-message settings-message--error" role="alert">{updateError ?? updateStatus?.errorMessage}</div>}
                    <div className="settings-panel__actions update-actions">
                      <Button variant="quiet" disabled={updateBusy} onClick={() => void checkForUpdate()}><RefreshCw size={15} />{updateBusy ? "Checking…" : "Check again"}</Button>
                      {updateStatus?.available && !updateStatus.available.reflashRequired && <Button variant="primary" disabled={updateBusy || updateStatus.state !== "available"} onClick={() => void installUpdate()}><Download size={15} />Install & restart</Button>}
                    </div>
                  </div>
                )}
              </section>
            ) : (
              <section className="settings-panel" aria-labelledby="security-heading">
                <div className="settings-panel__heading">
                  <p className="eyebrow">Local device access</p>
                  <h2 id="security-heading">Security & reset</h2>
                  <p>Local credentials are separate from the hosted Ardor account. This direct connection is intended only for a trusted LAN.</p>
                </div>
                {error && <div className="settings-message settings-message--error" role="alert">{error}</div>}
                {factoryNotice && <div className="settings-message settings-message--success" role="status">{factoryNotice}</div>}
                <div className="security-actions">
                  <article><div><LogOut size={18} /><span><strong>Sign out this browser</strong><small>Ends only the current local session.</small></span></div><Button variant="quiet" disabled={securityBusy} onClick={() => void logoutLocal()}>Sign out</Button></article>
                  <article><div><RotateCcw size={18} /><span><strong>Reset local access</strong><small>Removes the local account and all sessions, while preserving sounds and network settings.</small></span></div><Button variant="danger" disabled={securityBusy} onClick={() => void resetLocal()}>Reset access</Button></article>
                  <article className="security-actions__factory"><div><Trash2 size={18} /><span><strong>Factory reset</strong><small>Erases all user content and Wi-Fi after physical confirmation on the pedal.</small></span></div><Button variant="danger" disabled={securityBusy || Boolean(factoryNotice)} onClick={() => void beginFactoryReset()}>Factory reset…</Button></article>
                </div>
              </section>
            )}
          </div>
          </Dialog.Content>
        </div>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
