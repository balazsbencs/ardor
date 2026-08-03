# Hosted manager architecture

Status: active implementation contract; Phase 1 foundation implemented

This document defines the target architecture for managing an Ardor pedal from
a verified HTTPS website while preserving local and offline operation. It is a
design contract, not a claim that the hosted components already exist.

## Goals

- Make the primary experience a normal HTTPS website with no installed client.
- Let a user claim a physical pedal and see it automatically after signing in.
- Avoid inbound ports, router configuration, local TLS certificates, IP entry,
  and browser access from a public origin to an HTTP LAN service.
- Keep presets, audio assets, and realtime authority on the pedal initially.
- Preserve the embedded manager as an offline and recovery interface.
- Reuse the same React product and device API semantics in hosted, local, and
  Tauri modes.
- Integrate TONE3000 without placing OAuth tokens or secret keys in firmware or
  browser storage.
- Make account removal, local access reset, and factory reset distinct and
  recoverable operations.

## Non-goals for the first release

- Cloud synchronization or backup of the user's complete preset library.
- Remote firmware updates.
- Multi-region high availability.
- Arbitrary shell, filesystem, or URL commands sent from the control plane.
- Remote access to the realtime audio callback or sample-by-sample parameters.
- End-to-end encryption that prevents the Ardor service from reading commands.
- A public plugin marketplace or competing TONE3000 catalog.

## Existing baseline

The current repository already provides useful boundaries:

- `apps/manager` is a React application with a typed HTTP client.
- `services/managerd` owns assets, presets, Wi-Fi settings, and runtime command
  queuing outside the realtime process.
- The device-hosted manager is embedded into `ardor-managerd` and served from
  the same origin as `/api`.
- The audio process consumes bounded command files rather than accepting
  network requests directly.
- The Buildroot image uses a read-only root filesystem and a writable
  `/opt/ardor-pedal` data partition.
- NAM, including the fast A2 path, is loaded outside the realtime callback.

The current static bearer-token authentication is transitional. The shipping
overlay currently disables it and must not be treated as the target security
model.

## System overview

```text
                         Internet

  Browser                                       TONE3000
  ┌──────────────────┐                          ┌──────────────┐
  │ Hosted React UI  │◄──── OAuth redirect ───►│ OAuth + API  │
  └────────┬─────────┘                          └──────┬───────┘
           │ HTTPS                                     │ HTTPS
           ▼                                           ▼
  ┌────────────────────────────────────────────────────────────┐
  │ Ardor control plane                                       │
  │ accounts · sessions · device ACLs · jobs · audit events   │
  │ HTTPS API · WebSocket gateway · temporary asset delivery  │
  └───────────────────────────┬────────────────────────────────┘
                              │ outbound WSS/TLS
                              ▼
  ┌────────────────────────────────────────────────────────────┐
  │ Ardor pedal                                                │
  │ device agent / managerd → validated runtime command queue  │
  │ local React UI at http://ardor-<id>.local                  │
  └────────────────────────────────────────────────────────────┘

                         Local network
```

The pedal always initiates the cloud connection. The control plane never
connects inbound to the user's LAN.

## Components

### Hosted manager

`apps/manager` remains the product UI. A transport abstraction separates UI
state from connectivity:

```ts
interface ManagerTransport {
  getDevice(): Promise<DeviceStatus>;
  listPresets(): Promise<PresetSlotSummary[]>;
  getPreset(bank: number, slot: number): Promise<PresetSlot>;
  savePreset(bank: number, slot: number, preset: Preset): Promise<PresetSlot>;
  applyPreset(bank: number, slot: number): Promise<ApplyPresetResponse>;
  uploadAsset(kind: AssetKind, file: File): Promise<Job>;
}
```

- `LocalTransport` calls the existing same-origin `/api` endpoints.
- `CloudTransport` calls the hosted API with a selected `deviceId`.
- `TauriTransport` may initially retain a direct LAN connection but should use
  the same cloud API when the user signs in.

The cloud build must not contain device credentials, TONE3000 tokens, or a
server-side TONE3000 secret.

### Control plane

The proposed repository surface is `services/controlplane`, implemented in Go
to share conventions with `managerd`. Its first deployment consists of:

- An HTTPS JSON API for accounts, claiming, devices, and jobs.
- A WebSocket gateway for authenticated pedal connections.
- SQLite in WAL mode for durable identity, authorization, session, job, and
  audit data in the first single-instance deployment.
- Optional S3-compatible storage for short-lived asset transfers.
- A fixed TONE3000 OAuth callback.

A single service instance is the first-release topology. SQLite keeps that
deployment operationally simple: one durable file, transactional claiming, and
straightforward backups. PostgreSQL, horizontal WebSocket routing, Redis, or a
message broker are deferred until multiple service instances or sustained
write concurrency make them necessary. The wire protocol and repository
boundary do not assume that a connection always terminates on the same process
in the future.

### Device agent

The first implementation can live in `ardor-managerd`. It owns:

- Device identity and cloud session establishment.
- The outbound reconnecting WebSocket.
- Schema validation and authorization context for remote commands.
- Job execution, progress, cancellation, and idempotency.
- Direct calls into the same stores used by the local API.

Cloud work must stay outside the realtime audio callback. Applying a preset or
reloading assets continues through the existing durable runtime command queue.

## Account authentication

The hosted account uses a normalized username and password. Email is not
required for the initial design.

- Hash passwords with Argon2id and a unique random salt.
- Store password-hash parameters with the hash so they can be upgraded.
- Require at least 12 characters and allow at least 128 characters.
- Allow paste and password managers; do not impose composition rules.
- Rate-limit by account and source address with exponential backoff.
- Issue 256-bit opaque sessions, storing only their hashes.
- Use `Secure; HttpOnly; SameSite=Strict; Path=/` cookies on the hosted origin.
- Rotate the session identifier after login and password changes.
- Provide logout for one session and revocation of all sessions.
- Generate printable single-use recovery codes at account creation.

Account recovery consumes a recovery code and invalidates every existing
session. Adding verified email recovery or passkeys later must not weaken the
recovery-code path.

## Device identity

On first boot, the pedal generates:

- A random stable device identifier.
- An Ed25519 root key pair.
- A monotonically increasing claim epoch.

The private key is written with mode `0600` under
`/opt/ardor-pedal/identity`. It is never returned by an API, copied into an
image, or uploaded to the control plane. The public key and device identifier
are not secrets.

The root identity survives account removal, local access reset, and factory
reset. A complete data-partition erase or reflash creates a new identity. This
is an acceptable limitation for the initial DIY hardware target.

The root key authenticates enrollment and reset statements. Short-lived cloud
connection credentials may be rotated without replacing it.

## Claiming protocol

Claiming binds a physical device to an authenticated account. A code alone is
not sufficient; the pedal requires physical confirmation.

### State machine

```text
UNCLAIMED
   │ device requests enrollment challenge
   ▼
CODE_VISIBLE ── expires/cancel ──► UNCLAIMED
   │ account submits code
   ▼
CONFIRM_ON_DEVICE ── reject/expire ──► UNCLAIMED
   │ physical confirmation + signed challenge
   ▼
CLAIMED
   │ account unclaim or physical reset
   ▼
UNCLAIMED
```

### Flow

1. The device opens TLS to the enrollment API and proves possession of its
   root private key by signing a server nonce.
2. The control plane returns a short-lived, human-readable claim code and QR
   payload. The code expires after ten minutes.
3. An authenticated user submits the code through the hosted manager.
4. The control plane sends the account display name and a fresh claim nonce to
   the connected pedal.
5. The pedal displays the pending claim. The user confirms on the touchscreen
   or with a boot-safe footswitch gesture.
6. The pedal signs the claim nonce, account identifier, and next claim epoch.
7. The control plane atomically creates the device membership and audit event.
8. The claim code becomes unusable regardless of outcome.

Claim attempts are rate-limited. Codes contain enough entropy to resist online
guessing during their short lifetime; QR payloads may contain a longer secret
than the manual code.

The first release supports one owner account per device. The schema should use
a membership table so explicit shared access and roles can be added later.

## Device connection protocol

### Establishment

1. Device requests a challenge over HTTPS using its device identifier.
2. Device signs the nonce, timestamp, protocol version, and claim epoch.
3. The control plane verifies the signature and current claim state.
4. The control plane issues a short-lived, audience-bound connection token.
5. Device opens `wss://<host>/v1/device/connect` with the token in an
   `Authorization` header, never in the URL.

The device verifies the normal public Web PKI certificate chain. No custom LAN
certificate authority is required.

### Reconnection

- Exponential backoff with jitter, capped at five minutes.
- Network loss never stops the audio process or local UI.
- The device reports its last completed durable job after reconnecting.
- Ephemeral UI reads fail when offline; durable jobs remain queryable.
- A second valid connection for the same device supersedes the older one.

### Message envelope

Every WebSocket message uses a versioned envelope:

```json
{
  "version": 1,
  "messageId": "018f...",
  "kind": "request",
  "operation": "preset.save",
  "issuedAt": "2026-08-03T18:00:00Z",
  "expiresAt": "2026-08-03T18:00:30Z",
  "payload": {}
}
```

Responses have their own `messageId`, carry the request ID in `correlationId`,
and contain either a typed result or a stable error code. Durable operations
also carry an idempotency key. The device
rejects unknown versions, operations, fields where strict schemas apply,
expired messages, oversized payloads, and replayed durable commands.

Allowed operations are explicit domain actions such as `preset.save`,
`preset.apply`, `asset.install`, and `wifi.update`. The protocol never exposes
generic filesystem paths, shell commands, or arbitrary download URLs.

## Authorization

The control plane authorizes every browser request before it reaches a device:

```text
authenticated session
  → account/device membership
  → operation permission
  → device online or durable-job eligibility
  → audit event
  → command dispatch
```

The device independently validates the command schema and its current claim
epoch. Cloud authorization is not a reason to accept an unbounded operation on
the device.

Initial roles:

- `owner`: all management operations, unclaim, and account-level reset.
- `device`: can act only as its own device connection and job worker.
- `service`: narrowly scoped background work such as expired-object cleanup.

Additional human roles are deferred.

## Data model

Minimum durable control-plane tables:

| Table | Purpose |
| --- | --- |
| `accounts` | Username, password hash, state, timestamps |
| `recovery_codes` | Account-bound one-time code hashes |
| `sessions` | Opaque session hashes, expiry, revocation |
| `devices` | Device ID, public key, claim epoch, status metadata |
| `device_memberships` | Account/device ownership and future roles |
| `claim_flows` | Expiring one-time claim challenges |
| `jobs` | Durable operation state and idempotency |
| `oauth_connections` | Encrypted TONE3000 tokens and metadata |
| `audit_events` | Security and device-management events |

The cloud does not initially store presets, NAM models, IRs, Wi-Fi passwords,
audio, or a copy of the pedal data partition. Device status is treated as a
short-lived cache, not the source of truth.

## Asset transfer

Large uploads use a durable job rather than the command socket:

1. Browser requests an upload job for a device and asset kind.
2. Control plane returns a short-lived object-storage upload URL.
3. Browser uploads with a fixed maximum size and content length.
4. Control plane marks the object ready and dispatches `asset.install`.
5. Device downloads only from the configured Ardor asset origin.
6. Device streams to a bounded temporary file, validates type and filename,
   syncs, and atomically renames it into place.
7. Device queues the existing asset reload and reports completion.
8. Temporary cloud storage expires automatically.

The device does not accept a client-provided URL. It receives an opaque object
identifier and constructs or obtains a single-use Ardor download URL. Redirects
to other origins are rejected to prevent server-side request forgery.

Initial size limits must be defined separately for NAM and IR assets and
enforced by browser, control plane, object storage, and device.

## TONE3000 integration

The hosted control plane uses TONE3000's OAuth Select flow with PKCE and one
fixed callback:

```text
https://<host>/v1/integrations/tone3000/callback
```

1. Authenticated user starts a flow for a selected Ardor device.
2. Control plane generates state, verifier, and challenge and stores the
   verifier server-side with a short expiry.
3. Browser is redirected to TONE3000 with `prompt=select_tone` and
   `format=nam`.
4. Callback validates and consumes state before exchanging the code.
5. Access and refresh tokens are encrypted at rest and never returned to the
   browser or pedal.
6. Control plane fetches tone and model metadata and displays it in the hosted
   manager.
7. User chooses a model. The control plane obtains the model using the user's
   OAuth access and stages it as a normal Ardor asset job.
8. Attribution, tone ID, model ID, architecture, license, and creator are saved
   as source metadata with the installed asset.

Ardor currently compiles NAM A2 support. Because TONE3000's architecture
filter is singular and omitting it excludes A2, the UI should ask whether to
browse A1/Custom or A2 before starting Select. The model is still validated by
the device before installation.

No TONE3000 secret key is embedded in the open-source UI, desktop binary, or
firmware. Account unlink and access reset delete stored OAuth tokens. The
integration must remain within TONE3000's applicable API and attribution terms.

## Local and offline authentication

The embedded manager remains available at
`http://ardor-<short-device-id>.local`. It cannot provide verified transport
security and must describe that limitation rather than presenting the
connection as encrypted.

Local recovery access is optional but recommended. During cloud claiming, the
owner can set a distinct local username and password through the HTTPS portal.
The control plane forwards the credential once over the authenticated device
WebSocket and never stores or logs it. The device stores only an Argon2id hash.

An offline-only device can create the same local account through the embedded
setup page using a temporary code displayed on the pedal. This path is
explicitly limited to a trusted LAN because the initial HTTP request is not
encrypted.

The local manager uses an opaque `HttpOnly; SameSite=Strict` session cookie.
It cannot use the `Secure` attribute over HTTP. State-changing requests also
require a matching `Origin` and `Host`; CORS remains allowlisted for supported
Tauri origins.

The hosted and local passwords are independent. The UI must discourage reuse.

## Reset and recovery

Reset operations have deliberately different scopes:

| Operation | Account binding | Local auth/sessions | TONE3000 | Presets/assets | Wi-Fi |
| --- | --- | --- | --- | --- | --- |
| Remove from account | Clear | Preserve | Cloud tokens removed | Preserve | Preserve |
| Reset local access | Preserve | Clear | Device-local tokens removed | Preserve | Preserve |
| Factory reset | Clear | Clear | All tokens removed | Clear | Clear |

### Remove from account

The owner initiates unclaim from the hosted service. The cloud increments the
claim epoch, revokes device authorization, closes the device socket, and sends
the signed unclaim state if the device is online. An offline device reconciles
the new epoch on its next root-authenticated connection.

### Reset local access

Available from authenticated settings or through a physical boot-time gesture.
It deletes the local password hash and every local session while preserving
content and network configuration. The device returns to local setup mode.

### Factory reset

Requires physical confirmation on the pedal, even if initiated remotely. It
clears account binding, local authentication, TONE3000 material, presets,
assets, Wi-Fi, and user settings. The stable root device identity and an audit
record of the reset epoch survive. If offline, cloud revocation is queued for
the next connection.

Destructive reset screens enumerate what will be removed and require a second
confirmation. A reset is implemented as a recoverable state machine with a
durable marker so power loss cannot leave mixed old and new authorization
state.

## Security model

### Assets to protect

- Device control, including active audio behavior and Wi-Fi configuration.
- Account credentials, recovery codes, sessions, and device ownership.
- Device root private keys and connection credentials.
- TONE3000 OAuth tokens and creator attribution.
- User presets and licensed or private NAM/IR assets.
- Service availability without compromising local audio availability.

### Trust boundaries

- Browser to hosted control plane over public HTTPS.
- Control plane to SQLite and object storage.
- Control plane to TONE3000.
- Control plane to pedal over outbound WSS.
- Local browser or Tauri client to the pedal's HTTP API.
- `managerd` to the realtime audio process through the data partition.
- Build and release systems to firmware, desktop, and hosted artifacts.

### Primary attacker stories and required controls

| Attacker story | Required control |
| --- | --- |
| Guess or steal a claim code | Short expiry, rate limits, one-time use, physical confirmation |
| Use one account to control another user's pedal | Membership check on every request and opaque identifiers |
| Replay a cloud command | Message expiry, idempotency, replay ledger, claim epoch |
| Steal a database and crack passwords | Argon2id, unique salts, recovery/session hashes |
| Steal a browser token through XSS | HttpOnly cookies, CSP, output encoding, no auth in Web Storage |
| Abuse asset installation for SSRF or path traversal | Opaque object IDs, origin allowlist, sanitized names, bounded files |
| Upload a malicious NAM, WAV, or preset | Size/schema validation, safe parsers, load outside realtime callback |
| Compromise the cloud service and control every pedal | Narrow device protocol, audit, revocation, physical gates for destructive operations |
| Intercept local HTTP | Explicit trusted-LAN assumption; local password distinct; cloud remains primary |
| Exhaust pedal resources remotely | Per-account/device limits, bounded queues, streaming, cancellation |

### Security invariants

- The public internet cannot initiate a network connection to a pedal.
- A claim is never completed without device-key proof and physical presence.
- An account can operate only devices authorized by an active membership.
- Reset or unclaim invalidates previous sessions and claim epochs.
- Device root private keys and password plaintext are never persisted remotely.
- TONE3000 tokens never enter browser storage or firmware.
- Cloud messages map only to bounded domain operations.
- Network, parsing, storage, and model-loading work never runs in the realtime
  audio callback.
- Loss of cloud connectivity never stops local audio or local recovery access.

The repository-wide security policy and threat model remain authoritative for
severity and reporting beyond this architecture-specific model.

## Availability and privacy

- The audio engine and local controls remain functional during any outage.
- Hosted reads clearly distinguish `offline`, `stale`, and `unknown` states.
- Cloud job retries are bounded and visible; they do not silently duplicate
  writes.
- Audit events record authentication, claiming, membership changes, resets,
  TONE3000 linking, and destructive device operations.
- Logs exclude passwords, cookies, recovery codes, private keys, OAuth tokens,
  Wi-Fi passwords, complete presets, and signed asset URLs.
- Retention periods are explicit. Temporary assets and OAuth state expire
  automatically; audit retention is configurable.

## API sketch

Hosted browser API:

```text
POST   /v1/auth/register
POST   /v1/auth/login
POST   /v1/auth/logout
POST   /v1/auth/logout-all
POST   /v1/auth/recover

GET    /v1/devices
POST   /v1/device-claims
GET    /v1/devices/{deviceId}
DELETE /v1/devices/{deviceId}/membership

GET    /v1/devices/{deviceId}/presets
PUT    /v1/devices/{deviceId}/presets/{bank}/{slot}
POST   /v1/devices/{deviceId}/presets/{bank}/{slot}/apply
POST   /v1/devices/{deviceId}/assets
GET    /v1/jobs/{jobId}

POST   /v1/integrations/tone3000/start
GET    /v1/integrations/tone3000/callback
POST   /v1/integrations/tone3000/install
DELETE /v1/integrations/tone3000
```

Device API:

```text
POST /v1/device/enrollment-challenge
POST /v1/device/connection-challenge
POST /v1/device/connection-token
GET  /v1/device/connect                 (WebSocket upgrade)
GET  /v1/device/assets/{objectId}       (single-use download)
```

Exact request and response schemas belong in versioned API definitions before
implementation. Identifiers in paths are never authorization checks by
themselves.

## Delivery phases

### Phase 1: protocol foundations

- Add the manager transport interface without changing local behavior.
- Add device identity generation and persistence.
- Define JSON schemas and shared protocol fixtures.
- Implement cloud connection and reconnect logic against a local test server.
- Keep all remote mutation feature flags disabled.

Acceptance: local/Tauri tests remain green; a simulated device authenticates,
reconnects, and rejects invalid or replayed envelopes.

Implemented on `feat/device-hosted-manager`: the local HTTP transport remains
the default; device identity is durable; protocol v1 schemas and fixtures are
shared under `protocol/cloud/v1`; and the cloud agent is opt-in with all remote
mutations refused.

### Phase 2: hosted accounts and claiming

- Add account, session, recovery-code, device, membership, claim, and audit
  persistence.
- Implement physical claim confirmation in the device UI.
- List device presence and status in the hosted manager.
- Implement unclaim and session revocation.

Acceptance: a code cannot claim without physical confirmation; users cannot
read or operate devices outside their memberships.

Implemented on `feat/device-hosted-manager`: the Go control plane uses a
migrated SQLite database and hardened cookie sessions; recovery codes are
one-time and hash-only; device root-key authentication, presence, claiming,
physical touchscreen/footswitch approval, claim-epoch reconciliation, unclaim,
and account-isolated device lists are covered by integration tests. The React
manager has a separate hosted build for account, recovery, claiming, and device
presence screens. Remote preset mutations remain disabled until Phase 3.

### Phase 3: preset management

- Route read, save, and apply operations through `CloudTransport`.
- Add idempotency and operation audit events.
- Preserve local operation during cloud outage.

Acceptance: hosted and local managers produce the same persisted preset and
runtime activation behavior.

### Phase 4: assets and TONE3000

- Add temporary object storage and bounded asset jobs.
- Move TONE3000 OAuth to the fixed hosted callback.
- Add A1/Custom versus A2 selection and source metadata.
- Remove browser token handling; retire the Tauri loopback callback after all
  supported clients use the hosted flow.

Acceptance: tokens remain server-side; interrupted downloads are recoverable;
the device rejects invalid content without disturbing active audio.

### Phase 5: local authentication and reset

- Replace static bearer configuration with local account setup and sessions.
- Add reset-local-access and physically confirmed factory-reset state machines.
- Add mDNS hostname and service advertisement.

Acceptance: a new device cannot be claimed locally by LAN access alone;
power loss during reset has a deterministic recovery outcome.

## Deferred decisions

- Production hosting provider and geographic region.
- Email recovery and passkeys.
- Multi-owner or guest roles.
- Cloud preset backup and conflict resolution.
- Whether temporary TONE3000 model bytes use object storage or a streaming
  proxy at initial scale.
- Hardware-backed device keys for a future production PCB.
- Firmware-update signing and OTA delivery.

These decisions do not block protocol foundations, account/device isolation,
or the first single-instance control plane.
