import type { ButtonHTMLAttributes, InputHTMLAttributes, ReactNode } from "react";

export function cx(...values: Array<string | false | null | undefined>): string {
  return values.filter(Boolean).join(" ");
}

export function Button({
  variant = "secondary",
  className,
  children,
  ...props
}: ButtonHTMLAttributes<HTMLButtonElement> & {
  variant?: "primary" | "secondary" | "quiet" | "danger";
}) {
  return <button className={cx("button", `button--${variant}`, className)} {...props}>{children}</button>;
}

export function IconButton({
  label,
  className,
  children,
  ...props
}: ButtonHTMLAttributes<HTMLButtonElement> & { label: string; children: ReactNode }) {
  return <button className={cx("icon-button", className)} aria-label={label} title={label} {...props}>{children}</button>;
}

export function Toggle({
  checked,
  onChange,
  label,
  disabled,
}: {
  checked: boolean;
  onChange(checked: boolean): void;
  label: string;
  disabled?: boolean;
}) {
  return (
    <label className="toggle-control">
      <input type="checkbox" checked={checked} disabled={disabled} onChange={(event) => onChange(event.target.checked)} />
      <span aria-hidden="true" className="toggle-control__track"><span className="toggle-control__thumb" /></span>
      <span className="sr-only">{label}</span>
    </label>
  );
}

export function NumberInput({ className, ...props }: InputHTMLAttributes<HTMLInputElement>) {
  return <input className={cx("number-input", className)} inputMode="decimal" {...props} />;
}

export function StatusBadge({
  tone = "neutral",
  children,
}: { tone?: "neutral" | "success" | "warning" | "danger" | "info"; children: ReactNode }) {
  return <span className={`status-badge status-badge--${tone}`}>{children}</span>;
}
