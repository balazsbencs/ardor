import * as Dialog from "@radix-ui/react-dialog";
import {
  Check,
  Eye,
  EyeOff,
  Palette,
  RotateCcw,
  Settings,
  Wifi,
  X,
} from "lucide-react";
import { type CSSProperties, useEffect, useState } from "react";

import type { WiFiSettings } from "../api/types";
import { Button, IconButton, StatusBadge, cx } from "../components/ui";
import { useDeviceSession } from "../connection/deviceSession";

type SettingsSection = "appearance" | "wifi";

const accentChoices = [
  { name: "Ardor green", value: "#c9ff3d" },
  { name: "Signal blue", value: "#67a6ff" },
  { name: "Stage amber", value: "#ffb347" },
  { name: "Violet", value: "#b88cff" },
  { name: "Coral", value: "#ff7b6b" },
];

export const defaultAccent = accentChoices[0].value;

function wifiTone(status?: string): "neutral" | "success" | "warning" {
  if (status === "connected") return "success";
  if (status === "connecting" || status === "restarting") return "warning";
  return "neutral";
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
  theme: "dark" | "light";
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

  const wifiAvailable = session.status === "connected"
    && Boolean(session.client)
    && session.device?.capabilities.wifiSettings === true;
  const accentInk = parseInt(accent.slice(1, 3), 16) * .299
    + parseInt(accent.slice(3, 5), 16) * .587
    + parseInt(accent.slice(5, 7), 16) * .114 > 150 ? "#0a0d0b" : "#ffffff";
  const portalStyle = {
    "--accent": accent,
    "--focus": accent,
    "--accent-ink": accentInk,
  } as CSSProperties;

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

  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal>
        <div className="app-shell settings-portal" data-theme={theme} style={portalStyle}>
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
            ) : (
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
            )}
          </div>
          </Dialog.Content>
        </div>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
