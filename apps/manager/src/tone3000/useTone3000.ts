import { useRef, useState } from "react";

import { useDeviceSession } from "../connection/deviceSession";
import { isHostedCloudRuntime } from "../runtime/platform";
import type { AssetKind } from "../api/types";
import { getHostedTone3000Selection, installHostedTone3000Model, startHostedTone3000Selection } from "./hosted";
import { getLocalTone3000Selection, installLocalTone3000Model, startLocalTone3000Selection } from "./local";
import type { Tone3000Phase } from "./Tone3000Dialog";
import type { Tone3000Selection } from "./types";

type Options = { kind?: AssetKind; onInstalled?: (notice: string) => void };

/** The TONE3000 browse and install flow, moved out of AssetLibrary unchanged. */
export function useTone3000(deviceId?: string, { kind = "models", onInstalled }: Options = {}) {
  const session = useDeviceSession();
  const hostedCloud = isHostedCloudRuntime();
  const [phase, setPhase] = useState<Tone3000Phase>("idle");
  const [selection, setSelection] = useState<Tone3000Selection>();
  const [selectedModelId, setSelectedModelId] = useState<number>();
  const [flowId, setFlowId] = useState<string>();
  const [error, setError] = useState<string>();
  const hostedFlowGeneration = useRef(0);
  const hostedPopup = useRef<Window | null>(null);
  const available = kind === "models" && (hostedCloud ? deviceId !== undefined : session.device?.capabilities.tone3000 === true);

  const launch = async () => {
    setError(undefined);
    if (hostedCloud && !deviceId) {
      setError("No hosted device is selected.");
      return;
    }
    const generation = ++hostedFlowGeneration.current;
    const popup = window.open("", "ardor-tone3000", "popup,width=1100,height=760");
    if (!popup) {
      setError("Allow pop-ups for Ardor to browse TONE3000.");
      return;
    }
    hostedPopup.current = popup;
    try {
      setPhase("waiting");
      const started = hostedCloud ? await startHostedTone3000Selection(deviceId!) : await startLocalTone3000Selection();
      if (generation !== hostedFlowGeneration.current) return;
      setFlowId(started.flowId);
      popup.location.replace(started.authorizeUrl);
      for (;;) {
        await new Promise((resolve) => window.setTimeout(resolve, 900));
        if (generation !== hostedFlowGeneration.current) return;
        const current = hostedCloud ? await getHostedTone3000Selection(started.flowId) : await getLocalTone3000Selection(started.flowId);
        if (current.status === "failed") throw new Error(current.message || "TONE3000 selection failed.");
        if (current.status === "ready" && current.selection) {
          setSelection(current.selection);
          setSelectedModelId(current.selection.models[0]?.id);
          setPhase("detail");
          popup.close();
          hostedPopup.current = null;
          return;
        }
      }
    } catch (reason) {
      popup.close();
      hostedPopup.current = null;
      setPhase("idle");
      setError(reason instanceof Error ? reason.message : "Could not open TONE3000.");
    }
  };

  const browse = () => {
    if (sessionStorage.getItem("ardor-manager.tone3000.introduced") === "1") {
      void launch();
    } else {
      setPhase("intro");
    }
  };

  const continueFlow = () => {
    sessionStorage.setItem("ardor-manager.tone3000.introduced", "1");
    void launch();
  };

  const cancel = () => {
    hostedFlowGeneration.current += 1;
    hostedPopup.current?.close();
    hostedPopup.current = null;
    setPhase("idle");
  };

  const install = async () => {
    const model = selection?.models.find(({ id }) => id === selectedModelId);
    if (!selection || !model) return;
    setError(undefined);
    setPhase("installing");
    try {
      if (!flowId) throw new Error("TONE3000 selection is no longer available.");
      if (hostedCloud) await installHostedTone3000Model(flowId, model.id);
      else await installLocalTone3000Model(flowId, model.id);
      await session.refreshAssets("models");
      setPhase("idle");
      setFlowId(undefined);
      onInstalled?.(`${model.name} by @${selection.tone.user.username} installed from TONE3000.`);
    } catch (reason) {
      setPhase("detail");
      setError(reason instanceof Error ? reason.message : "Could not install the Tone3000 model.");
    }
  };

  return { phase, selection, selectedModelId, setSelectedModelId, available, browse, continueFlow, cancel, install, error };
}
