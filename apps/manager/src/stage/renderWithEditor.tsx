import { render } from "@testing-library/react";
import type { ReactNode } from "react";

import { EditorProvider, usePresetEditorContext } from "../presets/editor/EditorContext";
import type { PresetEditor } from "../presets/editor/usePresetEditor";

/** Renders a node inside EditorProvider and exposes the live editor context. */
export function renderWithEditor(node: ReactNode): { editor: () => PresetEditor } {
  const ref: { current?: PresetEditor } = {};
  function Probe() {
    ref.current = usePresetEditorContext();
    return null;
  }
  render(<EditorProvider>{node}<Probe /></EditorProvider>);
  return { editor: () => ref.current as PresetEditor };
}
