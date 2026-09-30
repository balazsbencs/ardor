import { Send } from "lucide-react";

import { Button } from "../components/ui";
import { useDeviceSession } from "../connection/deviceSession";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { Tag } from "../ui/Tag";

/** The pedal plays saved slots only, so a changed draft offers Save and load. */
export function LiveState() {
  const editor = usePresetEditorContext();
  const session = useDeviceSession();
  if (session.busy.apply || editor.saving) return <Tag tone="line">Sending to the pedal</Tag>;
  if (editor.dirty) {
    return (
      <Button variant="secondary" disabled={!editor.validation.canApply} onClick={() => void editor.saveAndApply()}
        title="Saves the slot, then loads it on the pedal"><Send size={15} />Save and load</Button>
    );
  }
  if (editor.runtimeMatchesDraft) return <Tag tone="live" title="The pedal plays this saved preset">LIVE ON PEDAL</Tag>;
  return <Button variant="secondary" disabled={!editor.validation.canApply} onClick={() => void editor.apply()}><Send size={15} />Load on pedal</Button>;
}
