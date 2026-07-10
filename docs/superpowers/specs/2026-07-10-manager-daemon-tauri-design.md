# Ardor Manager Daemon And Tauri App Design

## Goal

Add a Mac-first desktop manager for transferring `.nam` and `.wav` assets to
the pedal and editing saved presets offline, using a separate REST management
daemon on the device as the long-term API boundary for desktop and future
mobile clients.

## Product Scope

V1 includes:

- A separate `ardor-managerd` REST daemon on the pedal.
- A Tauri desktop app for macOS first and Windows second.
- Tailwind CSS and DaisyUI in the desktop app.
- Light and dark mode.
- Device connection by hostname or IP address.
- Bearer-token auth by default.
- Configurable auth-off mode for development and local testing.
- `.nam` upload as model assets.
- `.wav` upload as cabinet IR assets.
- Preset list, fetch, offline edit, validation, save, and explicit apply.
- Editing only the known v1 preset schema.
- Preserving unknown/future preset fields and blocks where possible.

V1 does not include:

- Live parameter streaming while the user moves controls.
- Bluetooth file transfer.
- Cloud sync.
- User accounts.
- Multi-device fleet management.
- A polished phone UI.

## Architecture

The system has three processes or surfaces:

```text
Tauri desktop app -> HTTP REST -> ardor-managerd -> preset/assets data root
                                               \-> optional local apply request

existing pedal runtime -> realtime audio + LVGL UI
```

`ardor-managerd` is a separate device-management daemon. It owns HTTP request
handling, authentication, asset validation, asset writes, preset reads, atomic
preset writes, and optional apply requests. It never performs realtime audio
processing.

The existing pedal runtime remains responsible for audio, LVGL, hardware
controls, and realtime preset loading. V1 manager edits affect saved files. The
active sound changes only when the user explicitly applies a saved slot or the
pedal runtime otherwise loads that slot.

The OpenAPI contract is the implementation source of truth:

```text
docs/api/ardor-managerd.openapi.yaml
```

## Device API

The daemon exposes product-level resources instead of raw filesystem access:

```text
GET    /api/device
GET    /api/assets/models
POST   /api/assets/models
DELETE /api/assets/models/{assetId}
GET    /api/assets/irs
POST   /api/assets/irs
DELETE /api/assets/irs/{assetId}
GET    /api/presets
GET    /api/presets/banks/{bank}/slots/{slot}
PUT    /api/presets/banks/{bank}/slots/{slot}
POST   /api/presets/banks/{bank}/slots/{slot}/apply
```

The daemon stores uploaded assets under the configured pedal data root:

```text
models/<sanitized-name>.nam
irs/<sanitized-name>.wav
presets/bank-000/preset-0.json
```

API responses return relative asset paths, such as `models/clean.nam` and
`irs/open-back.wav`, because those are the paths stored in preset block assets.

## Authentication

Production/default mode requires an opaque bearer token for write operations
and for normal app usage. Development mode can disable auth with a daemon config
flag or environment variable such as:

```text
ARDOR_API_AUTH=off
```

`GET /api/device` reports `authEnabled` so the desktop app can show whether it
is connected to a protected or testing daemon.

The desktop app stores the token in the operating system credential store. The
first implementation can accept manual token entry. Pairing-code setup can be a
later layer over the same bearer token model.

## Preset Editing Model

The desktop app edits presets offline:

```text
fetch preset -> local draft -> validate -> save preset -> optional apply
```

Changing a slider or asset picker in the app does not alter the active pedal
sound. The user must save/sync the slot, then explicitly apply it if they want
the pedal to load that slot.

Editable v1 fields:

- Preset name.
- Bank and slot target.
- `global.inputGainDb`.
- `global.outputGainDb`.
- NAM block asset.
- Cab/IR block asset.
- Cab `params.mix`.
- Cab `params.levelDb`.
- Daisy FX blocks that the repo already supports: `mod`, `delay`, and
  `reverb`, with normalized parameter values.

Non-editable but preserved:

- Unknown block types.
- Unknown block fields.
- Unknown preset fields.
- Unsupported parameters.
- `global.safetyLimitDb`, which remains a protective limiter setting rather
  than a tone-control surface in V1.

The app should show unknown blocks as unsupported/read-only instead of deleting
or rewriting them.

## Validation And File Safety

The daemon validates every asset and preset write:

- `.nam` uploads are accepted only by the model endpoint.
- `.wav` uploads are accepted only by the IR endpoint.
- Asset filenames are sanitized to URL-safe local names.
- Absolute preset asset paths are rejected.
- Preset asset paths containing `..` are rejected.
- Bank range is `0..99`.
- Slot range is `0..3`.
- Preset version must be `1`.
- Routing must be `"serial"`.

Preset writes are atomic:

```text
write preset-<slot>.json.tmp
flush/close
rename over preset-<slot>.json
```

Upload writes should use the same temporary-file pattern so interrupted uploads
do not leave a partial asset with the final name.

## Tauri App UX

The first screen is the manager workspace, not a marketing page.

Primary areas:

- Connection panel: hostname/IP, token state, auth-enabled state, device status.
- Asset library: model assets and IR assets, with upload and delete actions.
- Preset browser: banks and four slots per bank.
- Preset editor: serial chain, known editable controls, unsupported read-only
  blocks, dirty state, save, discard, and apply.
- Settings: theme, API base URL, token management.

The UI uses DaisyUI components with a light/dark theme toggle. It should be
dense and utilitarian: this is a device-management tool, not a landing page.

## Platform Priorities

macOS is the first-class desktop target:

- Local development and first packaging should be verified on macOS.
- Token storage should use the macOS credential store through Tauri-compatible
  APIs where practical.
- The app should avoid macOS-only frontend assumptions so Windows packaging can
  follow with minimal changes.

Windows is second:

- The REST API and frontend state model must be platform-neutral.
- Windows-specific work is packaging, credential storage verification, and file
  picker behavior.

## Buildroot Integration

The daemon is packaged as a Buildroot package alongside the existing pedal app.

The package installs:

- `ardor-managerd`.
- An init script.
- A config file or environment file with data root, port, bind address, and
  auth mode.

Default bind behavior should be conservative. For early development the daemon
can bind to all local interfaces on the pedal LAN. Production can tighten this
once hotspot and pairing behavior are finalized.

## Error Handling

The daemon returns structured JSON errors with:

- `error`: stable machine-readable code.
- `message`: human-readable explanation.
- `details`: optional structured context.

The desktop app should keep local drafts intact when save or upload fails. It
should report validation errors without discarding user edits.

## Testing

Daemon tests should cover:

- Asset filename sanitization.
- Extension validation.
- Path traversal rejection.
- Preset parse and validation.
- Atomic preset write behavior.
- Auth-on and auth-off request handling.
- OpenAPI endpoint response shapes for the first client paths.

Desktop app tests should cover:

- API client request construction.
- Token/no-token behavior.
- Preset draft editing without network writes.
- Dirty state.
- Known block editing.
- Unknown block preservation.
- Light/dark theme state.

Integration tests should run the daemon against a temporary data root, upload a
model and IR, save a preset that references them, fetch the preset back, and
compare the important fields.

## Open Questions For Later

- Whether apply requests should use a file signal, Unix socket, localhost HTTP,
  or another narrow interface to the pedal runtime.
- Whether mDNS discovery ships in V1 or hostname/IP entry is enough.
- Whether the daemon should eventually serve the same web UI for phone browsers.
- Whether Bluetooth is useful later for discovery and pairing.
