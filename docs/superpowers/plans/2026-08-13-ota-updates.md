# Signed Application OTA Updates

## Objective

Allow an authenticated local user to discover the newest stable Ardor device
release on GitHub, install it without replacing user data or the running system
partition, restart the pedal applications, and automatically roll back when the
new release is unhealthy or power is lost mid-transaction.

The first OTA-capable release is a bootstrap image that existing devices must
flash once. OTA v1 updates application binaries and the device-hosted Manager
only. Kernel, boot files, Buildroot libraries, partition layout, updater code,
and init wrappers remain part of the immutable base image and require a reflash.

## Non-negotiable properties

- Never write the released full-disk image to a running device.
- Never modify the read-only root filesystem during an OTA transaction.
- Preserve presets, models, IRs, settings, Wi-Fi, identity, and local auth.
- Authenticate update mutations with the existing local session and origin
  checks. Do not expose installation through cloud remote mutations in v1.
- Accept only signed bundles for the fixed `balazsbencs/ardor` repository,
  stable GitHub Releases, Raspberry Pi 4, and AArch64.
- Keep the immutable `/usr/bin` applications as the final recovery fallback.
- Make activation and transaction-state writes atomic and durable.
- Roll back a candidate that does not pass manager and audio health checks.
- Default to manual installation. Background checks may be enabled later, but
  background installation is out of scope.

## Release contract

Each stable GitHub Release publishes these additional assets:

```text
ardor-device-<version>-linux-aarch64.tar.gz
ardor-device-<version>-linux-aarch64.manifest.json
ardor-device-<version>-linux-aarch64.manifest.sig
```

The deterministic tarball contains only:

```text
bin/ardor-pedal
bin/ardor-managerd
```

Manifest schema v1:

```json
{
  "schemaVersion": 1,
  "version": "0.1.24",
  "tag": "v0.1.24",
  "commit": "0123456789abcdef0123456789abcdef01234567",
  "target": "raspberry-pi-4",
  "arch": "aarch64",
  "minimumUpdaterVersion": "1.0.0",
  "minimumBaseVersion": "0.1.24",
  "bundle": {
    "name": "ardor-device-0.1.24-linux-aarch64.tar.gz",
    "size": 1234,
    "sha256": "..."
  },
  "files": [
    {"path": "bin/ardor-pedal", "size": 123, "mode": 493, "sha256": "..."},
    {"path": "bin/ardor-managerd", "size": 456, "mode": 493, "sha256": "..."}
  ]
}
```

The signature is an Ed25519 signature over the exact manifest bytes, encoded as
base64 with a trailing newline. The private key lives only in a protected GitHub
Actions secret. The matching public key is stored in the immutable image at
`/etc/ardor-update.pub`. A `keyId` field can be added in a later schema when key
rotation is needed; updater v1 supports the one bootstrap key only.

The updater rejects unknown manifest fields, non-canonical versions, downgrade
attempts, mismatched tag/version/asset names, duplicate files, unexpected files,
unsafe archive entries, files over their declared sizes, and incompatible base
or updater versions.

## User-visible update flow

1. The Manager reads the installed and last-known update state.
2. An authenticated check queries GitHub's newest stable release, selects the
   exact three OTA assets, downloads the small manifest and signature, verifies
   them with the pinned public key, and checks target and base compatibility.
3. The Manager shows version, release notes, size, and any reflash requirement.
4. Installation requires confirmation that audio will mute and Manager will
   disconnect briefly.
5. The install request names the exact checked version, preventing a check/install
   race. The daemon durably writes the already-verified release selection and
   expected bundle digest before returning `202`.
6. After the HTTP request completes, the daemon starts `/usr/bin/ardor-updater`
   with stdio and request descriptors detached. Download and verification occur
   while the current applications continue running.
7. The updater stages and atomically activates the candidate, restarts both
   applications, validates them, and either confirms or rolls back.
8. The Manager reconnects, reads durable status, and reloads itself after success
   so the newly embedded web UI is active.

## On-device layout

```text
/usr/bin/ardor-pedal                    immutable fallback
/usr/bin/ardor-managerd                 immutable fallback
/usr/bin/ardor-updater                  immutable transaction owner
/etc/ardor-release.json                 immutable base metadata
/etc/ardor-update.pub                   immutable signing public key
/opt/ardor-pedal/system/
  current -> releases/0.1.24
  previous -> releases/0.1.23            optional convenience link
  releases/<version>/
    manifest.json
    bin/ardor-pedal
    bin/ardor-managerd
  update/
    operation.json                       durable public operation state
    transaction.json                     durable activation/rollback state
    lock                                 exclusive updater lock
    downloads/                           temporary, pruned after completion
```

`S98ardor-managerd` and `S99ardor-pedal` resolve their executable from
`system/current/bin` and fall back to `/usr/bin`. They export the selected
manifest path so both processes report the same installed version.

`S97ardor-update-recovery` runs before both services. It clears a stale updater
lock and, if it finds a switched but unconfirmed transaction, restores the
previous link (or removes `current` to select the immutable fallback), records
`rolled_back`, syncs the directory, and allows normal boot to continue. A
`prepared` record is also compared with the live link so a power loss in the
narrow activation/phase-write window cannot leave an unconfirmed release active.

The transaction represents the factory fallback explicitly rather than as an
empty or guessed symlink target. Rollback to factory removes `current` only after
validating that it is the link created by the transaction.

## Durable state machine

Public operation states:

```text
idle -> checking -> available
available -> downloading -> verifying -> staged -> restarting -> validating
validating -> succeeded
any install state -> failed
restarting|validating -> rolled_back
```

The transaction record separately tracks durable phases:

```text
prepared -> switched -> confirmed
                   `-> rolled_back
```

Rules:

1. Download and verify while the current release keeps running.
2. Extract into a staging directory on the same filesystem as `releases`.
3. Fsync files and directories, then rename staging to the final version.
4. Write `prepared`, including the prior symlink target, and fsync.
5. Atomically rename a new `current` symlink into place and fsync.
6. Write `switched` and fsync before stopping services.
7. Stop pedal first so audio is muted, then stop managerd. The detached updater
   is not managed by either PID file and remains alive.
8. Start managerd and pedal, then validate for 30 seconds.
9. Require `/api/device` on loopback to report the candidate version, a live
   pedal child process, and fresh runtime telemetry.
10. Write `confirmed`, update `previous`, record success, delete the transaction,
    and prune canonical release directories other than the active and previous
    versions.
11. On any post-switch failure, restore the old target, restart both services,
    record `rolled_back`, and retain the failure reason for the Manager.

At boot, `prepared` is discarded only when `current` still names the prior
release. If `current` already names the candidate, recovery treats it like
`switched` and rolls it back. `switched` is always rolled back, while `confirmed`
is finalized as success. This intentionally favors availability over retrying an
ambiguous candidate.

## HTTP API

All endpoints require local authorization. Mutations also use the existing
same-origin checks.

### `GET /api/system/update/status`

Returns installed/base/updater versions, operation state, checked release,
timestamps, whether a reflash is required, and a stable error code/message.

### `POST /api/system/update/check`

Queries `https://api.github.com/repos/balazsbencs/ardor/releases/latest` with a
fixed User-Agent and GitHub API version. It accepts only a stable release and
the three exact, unique asset names derived from its tag. Release download URLs
must match the repository and tag. It downloads and verifies the manifest and
signature before recording the result as `available`. Conditional requests may
use the stored ETag.

### `POST /api/system/update/install`

Body: `{"version":"0.1.24"}`. The exact version must match the most recently
verified check result. The daemon atomically writes an install request containing
the exact URL, expected bundle size and digest, manifest, and signature. It
returns `202`, closes the HTTP request lifecycle, and only then starts the
detached updater; the updater never resolves `latest` again.

### `POST /api/system/update/cancel`

Optional v1 endpoint. It may cancel only `downloading`; activation cannot be
cancelled. It can be omitted from the first increment without affecting safety.

## Download and extraction boundaries

- GitHub metadata endpoint is fixed in the binary; configuration may disable
  updates but may not redirect production devices to an arbitrary repository.
- Initial asset URLs must be
  `https://github.com/balazsbencs/ardor/releases/download/<tag>/<asset>`.
- Redirects remain HTTPS and are limited to GitHub-owned release asset hosts.
- Apply connect, response-header, idle-body, and total-operation timeouts.
- Stream to a temporary file, hash while downloading, enforce declared and hard
  size ceilings, sync, then rename.
- Require enough free bytes for download, extraction, retained previous release,
  and a fixed safety margin. Never remove the active or rollback release to make
  room automatically.
- Parse gzip/tar in Go. Do not invoke `tar`, accept links, or derive destination
  paths directly from archive names.

## Storage and data compatibility

The bootstrap keeps the existing fixed data partition for the first increment.
Every install performs a conservative free-space preflight, retains at most the
active and rollback application releases, and refuses the update instead of
deleting user data. Expanding the final data partition to the remaining SD-card
capacity is deferred until its partitioning method and power-loss behavior have
been qualified on hardware.

OTA v1 does not perform irreversible data migrations. Every released candidate
must read data produced by the immediately previous stable version. A future
migration framework requires its own backup/restore and compatibility contract.

Factory reset preserves `identity` and `system` while removing user-owned data.
This makes factory reset reset configuration/content without silently downgrading
the installed application.

## Manager experience

Add an Updates section to the local-device Settings dialog. It shows installed
and latest versions, release notes link, size, compatibility, check time, and
operation progress. Installation requires a confirmation explaining that audio
will mute and Manager will disconnect briefly. During restart, reconnect with
bounded exponential backoff, read the durable status, and reload the page after
success so the new embedded UI is used. Errors distinguish network, no space,
signature, compatibility, startup-health, and rolled-back cases.

The desktop and device-hosted Manager may invoke the local API. The hosted cloud
Manager does not expose OTA v1.

## Implementation phases

### Phase 1: bootstrap and version contract

- Add strict release/manifest types and semantic-version comparison in
  `services/managerd/internal/update` with unit tests.
- Add build metadata loading and expose software/base/updater versions through
  `/api/device`.
- Add `--version` output to both device executables.
- Make the service wrappers select `system/current` with immutable fallback.
- Add release metadata, public-key configuration, target CA certificates, and
  update recovery to the Buildroot image.
- Preserve `system` during factory reset.

### Phase 2: updater transaction engine

- Implement GitHub release discovery, strict asset selection, signature and hash
  verification, bounded download/extraction, disk preflight, durable state, and
  exclusive locking.
- Add the detached `ardor-updater` command and recovery command.
- Add injectable filesystem, HTTP, service, clock, and health boundaries for
  deterministic failure/power-loss tests.

### Phase 3: daemon API and Manager UI

- Add coordinator and authenticated endpoints to managerd and OpenAPI.
- Add transport/types/client methods and the Settings Updates section.
- Implement restart reconnect/reload behavior and UI tests.

### Phase 4: release pipeline

- Build the device Manager before compiling managerd.
- Include Manager sources and lockfiles in the firmware/payload hash.
- Export both AArch64 binaries from Buildroot.
- Add deterministic bundle/manifest tooling and CI signing.
- Assert that the OTA payload binaries are byte-identical to the applications
  shipped in the versioned full image built by the same job.
- Independently verify the completed bundle, signature, hashes, ELF architecture,
  and exact release asset set before publishing.
- Gate publish on manager, managerd, C++, updater, and bundle checks and prevent
  out-of-order workflows from regressing GitHub's latest release.

### Phase 5: device qualification

- Flash the bootstrap image and update N -> N+1 on a real Pi.
- Cut power during download, after switch, and during validation.
- Force manager and pedal startup failures and verify rollback.
- Fill storage below the safety threshold and verify rejection.
- Verify preservation of all data/auth/identity and factory-reset behavior.
- Promote OTA eligibility only after hardware validation.

## CI acceptance matrix

- Manifest: valid, unknown field, malformed/canonical version, duplicate path,
  wrong target/arch, incompatible minimum, downgrade.
- Trust: valid signature, wrong key, altered manifest, altered bundle/file.
- Archive: traversal, absolute path, link, device, duplicate, missing, extra,
  oversized, truncated, gzip bomb.
- Transaction: failure and simulated interruption before/after every durable
  phase; repeated recovery and rollback are idempotent.
- Runtime: manager never starts, wrong reported version, pedal crash-loop,
  missing/stale telemetry, healthy candidate, healthy rollback.
- API: auth/origin enforcement, concurrent install conflict, stale checked
  version, durable status after manager restart.
- UI: compatible update, reflash-required release, progress, reconnect, success,
  rollback, and each actionable error class.

## Review record

The pre-implementation review changed the initial proposal in four places:

- The check step verifies the signed manifest before the UI may call a release
  available, and install persists that immutable selection instead of resolving
  `latest` a second time.
- The updater is an immutable detached helper, so stopping managerd cannot kill
  the process that owns activation and rollback.
- Factory fallback is explicit and rollback refuses to replace a `current` link
  it does not own.
- Release CI builds the device-hosted Manager first and packages the exact
  AArch64 binaries exported by the full-image build.

The implementation review then added stale-lock recovery, detection of the
activation/phase-write power-loss window, confirmed-transaction finalization,
bounded release pruning, redirect restrictions, and signing-key participation
in the firmware cache key.

## Rollout and operational prerequisites

1. Generate an Ed25519 signing key offline.
2. Commit only its public key and configure the private key as a protected
   GitHub Actions secret.
3. Decide whether stable OTA releases remain every `main` push or move to an
   explicit tag/manual promotion. Manual promotion is recommended for hardware.
4. Publish and flash the bootstrap image; older images cannot self-bootstrap.
5. Complete the real-device qualification matrix before enabling installation
   for general use.
6. Update `PRODUCT.md`, `BUILD.md`, `SECURITY.md`, and the OpenAPI document.
