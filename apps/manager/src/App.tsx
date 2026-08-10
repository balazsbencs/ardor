import { AppShell } from "./app/AppShell";
import { HostedManager } from "./cloud/HostedManager";
import { DeviceSessionProvider } from "./connection/deviceSession";
import { LocalDeviceManager } from "./localAuth/LocalDeviceManager";
import { isDeviceHostedRuntime, isHostedCloudRuntime } from "./runtime/platform";

export default function App() {
  if (isHostedCloudRuntime()) return <HostedManager />;
  if (isDeviceHostedRuntime()) return <LocalDeviceManager />;
  return <DeviceSessionProvider><AppShell /></DeviceSessionProvider>;
}
