import tone3000Logo from "./tone3000-logo.svg";

export function Tone3000Brand({ compact = false }: { compact?: boolean }) {
  return (
    <span className={compact ? "tone3000-brand tone3000-brand--compact" : "tone3000-brand"}>
      <img src={tone3000Logo} alt="TONE3000" />
      {!compact && <small>NAM Captures and IRs</small>}
    </span>
  );
}
