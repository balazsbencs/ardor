import { Music2 } from "lucide-react";
import { Suspense, lazy, useEffect, useRef, useState, type ChangeEvent, type KeyboardEvent } from "react";

import type { Asset, AssetKind } from "../api/types";
import { useDeviceSession } from "../connection/deviceSession";
import type { PresetLocation } from "../presets/editor/editorTypes";
import { useTone3000 } from "../tone3000/useTone3000";
import { AssetsRail } from "./AssetsRail";
import { FileDrawer } from "./FileDrawer";
import { FileList } from "./FileList";
import { KindBar } from "./KindBar";
import { KIND_EXTENSIONS } from "./libraryView";
import { useAssetLibrary } from "./useAssetLibrary";
import "../stage/modules.css";
import "./assets.css";

const Tone3000Dialog = lazy(() => import("../tone3000/Tone3000Dialog").then((module) => ({ default: module.Tone3000Dialog })));

export type AssetsViewProps = {
  initialKind?: AssetKind;
  tone3000DeviceId?: string;
  pendingFiles?: File[];
  onFilesTaken(): void;
  onOpenPreset(location: PresetLocation, blockPath?: string): void;
  onBack?(): void;
};

export function AssetsView({ initialKind, tone3000DeviceId, pendingFiles, onFilesTaken, onOpenPreset, onBack }: AssetsViewProps) {
  const session = useDeviceSession();
  const library = useAssetLibrary(initialKind);
  const tone3000 = useTone3000(tone3000DeviceId, { kind: library.kind, onInstalled: (message) => { library.setKind("models"); library.announce(message); } });
  const [renamingId, setRenamingId] = useState<string>();
  const input = useRef<HTMLInputElement>(null);
  const replaceTarget = useRef<Asset | undefined>(undefined);
  const taken = useRef<File[] | undefined>(undefined);
  const connected = session.status === "connected";

  useEffect(() => {
    if (library.kind === "reverb-irs" && !session.supportsReverbIrs) library.setKind("models");
    // eslint-disable-next-line react-hooks/exhaustive-deps -- only the support flag can force a kind change
  }, [library.kind, session.supportsReverbIrs]);

  // Files dropped on the window arrive here once. The ref keeps a StrictMode remount from queueing them twice.
  useEffect(() => {
    if (!pendingFiles || pendingFiles.length === 0 || taken.current === pendingFiles) return;
    taken.current = pendingFiles;
    if (connected) library.enqueue(pendingFiles);
    onFilesTaken();
    // eslint-disable-next-line react-hooks/exhaustive-deps -- run once per handed-over batch
  }, [pendingFiles]);

  const pick = (replace?: Asset) => {
    const element = input.current;
    if (!element) return;
    replaceTarget.current = replace;
    element.accept = KIND_EXTENSIONS[library.kind];
    element.multiple = replace === undefined;
    element.click();
  };
  const picked = (event: ChangeEvent<HTMLInputElement>) => {
    const files = Array.from(event.target.files ?? []);
    event.target.value = "";
    const target = replaceTarget.current;
    replaceTarget.current = undefined;
    if (target && files[0]) library.replaceFile(target, files[0]);
    else library.enqueue(files);
  };

  const openFile = library.files.find(({ id }) => id === library.openId);
  const closeDrawer = () => { setRenamingId(undefined); library.setOpenId(undefined); };
  const renameFile = (asset: Asset) => { library.setOpenId(asset.id); setRenamingId(asset.id); };
  const submitRename = async (asset: Asset, filename: string) => {
    const problem = await library.rename(asset, filename);
    if (!problem) setRenamingId(undefined);
    return problem;
  };
  const onKeyDown = (event: KeyboardEvent) => {
    if (event.key !== "Escape") return;
    if (library.confirmDelete) library.cancelDelete();
    else if (renamingId) setRenamingId(undefined);
    else if (library.openId) closeDrawer();
  };

  if (!connected) {
    return (
      <div className="assets assets--offline">
        <div className="offline"><Music2 size={32} aria-hidden="true" /><h1>Connect to manage files</h1>
          <p>NAM models, cabinet IRs and reverb IRs live on the pedal. Connect to upload, rename or delete them.</p></div>
      </div>
    );
  }

  return (
    <div className="assets" onKeyDown={onKeyDown}>
      <KindBar kind={library.kind} onKind={(next) => { setRenamingId(undefined); library.setKind(next); }}
        inventory={{ models: session.models, irs: session.irs, reverbIrs: session.reverbIrs }}
        showReverb={session.supportsReverbIrs} missingCount={library.missing.length} />
      <FileList library={library} extraError={tone3000.error} onPick={() => pick()} onRename={renameFile} onOpenPreset={onOpenPreset} />
      {openFile && (
        <FileDrawer asset={openFile} kind={library.kind} usage={library.usage} renaming={renamingId === openFile.id}
          onRenameStart={() => setRenamingId(openFile.id)} onRenameSubmit={(name) => submitRename(openFile, name)}
          onRenameCancel={() => setRenamingId(undefined)} onReplace={() => pick(openFile)}
          onDelete={() => library.askDelete([openFile.id])} onClose={closeDrawer} onOpenPreset={onOpenPreset} />
      )}
      <AssetsRail library={library} canBrowseTone3000={tone3000.available} onPick={() => pick()} onBrowseTone3000={tone3000.browse} onBack={onBack} />
      <input ref={input} type="file" hidden multiple onChange={picked} />
      {tone3000.phase !== "idle" && (
        <Suspense fallback={null}>
          <Tone3000Dialog phase={tone3000.phase} selection={tone3000.selection} selectedModelId={tone3000.selectedModelId}
            onSelectedModelId={tone3000.setSelectedModelId} onContinue={tone3000.continueFlow} onCancel={tone3000.cancel} onInstall={() => void tone3000.install()} />
        </Suspense>
      )}
    </div>
  );
}
