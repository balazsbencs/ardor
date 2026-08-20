# Security Policy

## Supported versions

Security fixes are applied to the `main` branch and, when practical, to the
latest published release. Older firmware releases may not receive security
updates.

The Manager is a web application. The device serves it over LAN HTTP from
`ardor-managerd`, and the control plane serves the hosted build over HTTPS.
Ardor publishes no desktop application.

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

Device OTA updates accept only the fixed Ardor GitHub repository, exact release
asset names, and an Ed25519-signed strict manifest whose public key is pinned in
the immutable bootstrap image. A checksum without a valid signature is not an
authorized update. Keep the signing private key in a protected release secret,
rotate the bootstrap image if that key is suspected to be compromised, and do
not enable remote/cloud-triggered installation.
