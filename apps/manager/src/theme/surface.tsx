import { createContext, useContext, type ReactNode } from "react";

import { accentVariables, defaultAccent, type Theme } from "./accent";

const SurfaceContext = createContext<{ theme: Theme; accent: string }>({
  theme: "dark",
  accent: defaultAccent,
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
  const { theme, accent } = useSurface();
  return (
    <div className="app-shell portal-surface" data-theme={theme} style={accentVariables(accent, theme)}>
      {children}
    </div>
  );
}
