# Security Policy

## Supported versions

Security fixes are applied to the `main` branch and, when practical, to the
latest published release. Older firmware and desktop-manager releases may not
receive security updates.

Published desktop-manager support is limited to Apple Silicon and Intel macOS,
and Windows x64. Linux builds of the Tauri desktop manager are not supported or
released. Linux remains supported for the headless engine, manager daemon, and
firmware build tooling.

## Reporting a vulnerability

Please do not open a public issue or discussion for a suspected vulnerability.
Use [GitHub private vulnerability reporting](https://github.com/balazsbencs/ardor/security/advisories/new)
to share the details privately with the maintainer.

Include the affected component and version or commit, the expected and observed
behavior, reproduction steps or a proof of concept, the likely impact, and any
suggested mitigation. Remove credentials, personal data, licensed audio assets,
and device-specific secrets from the report.

You should receive an acknowledgement within seven days. The maintainer will
work with you to validate the report, coordinate a fix and disclosure timeline,
and credit you if desired. Please allow a reasonable remediation period before
public disclosure.

## Security-sensitive configuration

Ardor can expose a manager API on the local network and produces bootable
firmware images. Deployments should enable API authentication, use a strong
unique token, restrict network access to trusted clients, and avoid embedding
Wi-Fi credentials or other secrets in source-controlled firmware overlays.

Treat Neural Amp Modeler files, impulse responses, presets, and imported audio
as untrusted input. Only use assets from trusted sources on production devices.

## Accepted dependency risks

The universal Cargo lock file includes `glib` 0.18.x through Tauri's Linux-only
GTK/WebKit dependency graph. This version is affected by
[GHSA-wrw7-89jp-8q8g](https://github.com/advisories/GHSA-wrw7-89jp-8q8g),
whose first patched release is `glib` 0.20.0. The current Tauri dependency graph
cannot resolve that version.

This risk is accepted because Ardor neither supports nor publishes a Linux
Tauri manager, and the dependency is absent from the supported macOS and Windows
manager targets. The exception must be removed before adding Linux manager
support, or when a Tauri update makes `glib` 0.20.0 or newer resolvable.
