import { AppShell } from "./app/AppShell";
import { HostedManager } from "./cloud/HostedManager";
import { DeviceSessionProvider } from "./connection/deviceSession";
import { isHostedCloudRuntime } from "./runtime/platform";

export default function App() {
  if (isHostedCloudRuntime()) return <HostedManager />;
  return <DeviceSessionProvider><AppShell /></DeviceSessionProvider>;
}
