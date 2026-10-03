import type { ReactNode } from "react";

export function Tag({ tone = "line", title, children }: { tone?: "warn" | "live" | "ink" | "line" | "danger" | "scene"; title?: string; children: ReactNode }) {
  return <span className={`lb-tag lb-tag--${tone}`} title={title}>{children}</span>;
}
