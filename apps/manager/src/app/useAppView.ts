import { useCallback, useEffect, useState } from "react";

import type { AssetKind } from "../api/types";

export type AppView = "edit" | "assets";
type View = { view: AppView; assetKind?: AssetKind };
const KINDS: AssetKind[] = ["models", "irs", "reverb-irs"];

function fromHash(hash: string): View {
  const [name, kind] = hash.replace(/^#/, "").split("/");
  if (name !== "assets") return { view: "edit" };
  return { view: "assets", assetKind: KINDS.includes(kind as AssetKind) ? kind as AssetKind : undefined };
}

/** The Edit or Assets view, kept in the URL hash (`#assets`, `#assets/irs`) so a reload keeps it. */
export function useAppView() {
  const [state, setState] = useState<View>(() => fromHash(window.location.hash));
  useEffect(() => {
    const onHash = () => setState(fromHash(window.location.hash));
    window.addEventListener("hashchange", onHash);
    return () => window.removeEventListener("hashchange", onHash);
  }, []);
  const goto = useCallback((view: AppView, kind?: AssetKind) => {
    const hash = view === "assets" ? `#assets${kind ? `/${kind}` : ""}` : "";
    window.history.replaceState(null, "", `${window.location.pathname}${window.location.search}${hash}`);
    setState({ view, assetKind: view === "assets" ? kind : undefined });
  }, []);
  return { ...state, goto };
}
