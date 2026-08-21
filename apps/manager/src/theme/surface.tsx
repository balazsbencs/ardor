import { createContext, useContext, type ReactNode } from "react";

import { defaultPalette, paletteVariables, type PaletteId } from "./accent";

const SurfaceContext = createContext<{ palette: PaletteId }>({
  palette: defaultPalette,
});

export const SurfaceProvider = SurfaceContext.Provider;

export function useSurface() {
  return useContext(SurfaceContext);
}

/**
 * Radix mounts portal content on document.body, outside `.app-shell`, where none of the
 * design tokens inherit. Every portalled dialog renders its own token scope through this.
 */
export function PortalSurface({ children }: { children: ReactNode }) {
  const { palette } = useSurface();
  return (
    <div className="app-shell portal-surface" data-palette={palette} style={paletteVariables(palette)}>
      {children}
    </div>
  );
}
