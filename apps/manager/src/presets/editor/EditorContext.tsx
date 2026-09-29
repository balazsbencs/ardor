import { createContext, useContext, type ReactNode } from "react";

import { usePresetEditor, type PresetEditor } from "./usePresetEditor";

const EditorContext = createContext<PresetEditor | undefined>(undefined);

/** Holds the preset draft above the Edit and Assets views, so a view switch keeps unsaved edits. */
export function EditorProvider({ children }: { children: ReactNode }) {
  const editor = usePresetEditor();
  return <EditorContext.Provider value={editor}>{children}</EditorContext.Provider>;
}

export function usePresetEditorContext(): PresetEditor {
  const editor = useContext(EditorContext);
  if (!editor) throw new Error("usePresetEditorContext needs an EditorProvider");
  return editor;
}
