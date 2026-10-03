import { useDeviceSession } from "../connection/deviceSession";
import { isHostedCloudRuntime } from "../runtime/platform";

const HTTP_NOTE = "LAN access uses plain HTTP. Use it on trusted networks only.";

/** Device name, address and, on the LAN build, the plain HTTP warning (PRODUCT.md). */
export function ConnectionPill({ onOpen }: { onOpen(): void }) {
  const session = useDeviceSession();
  const hosted = isHostedCloudRuntime();
  const connected = session.status === "connected";
  const address = session.baseUrl.replace(/^https?:\/\//, "").replace(/\/$/, "");
  const name = connected
    ? session.device?.deviceName ?? "Ardor Pedal"
    : session.status === "error" ? "Connection error" : "Not connected";
  const label = connected ? session.device?.deviceName ?? "Connected" : session.status === "error" ? "Connection error" : "Disconnected";
  return (
    <button type="button" className={`conn conn--${session.status}`} onClick={onOpen} aria-label={`Device: ${label}`} title="Open device connection">
      <span className="conn__dot" aria-hidden="true" />
      <b>{name}</b>
      {connected && <span className="conn__addr">{address}</span>}
      {!hosted && <span className="conn__http" title={HTTP_NOTE}>HTTP</span>}
    </button>
  );
}
