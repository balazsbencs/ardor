import { AppShell } from "./app/AppShell";
import { DeviceSessionProvider } from "./connection/deviceSession";

export default function App() {
  return <DeviceSessionProvider><AppShell /></DeviceSessionProvider>;
}
