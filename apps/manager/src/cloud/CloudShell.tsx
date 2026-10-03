import type { ReactNode } from "react";

import { cx } from "../components/ui";
import { paletteVariables } from "../theme/accent";
import "./cloud.css";

/** The token scope for the sign-in and device screens, which render outside the edit shell. */
export function CloudShell({ className, children }: { className?: string; children: ReactNode }) {
  return <div className={cx("cloud-shell", className)} data-palette="slate" style={paletteVariables("slate")}>{children}</div>;
}
