# OTA update validation — 2026-10-23

Validation ran on Linux amd64 without connecting to the pedal. Tests use generated
Ed25519 keys, signed bundles, temporary release trees, fake service scripts, and
simulated GitHub/health responses. No device or published release was changed.

## Reproduced failures and changes

1. **Every normal persisted install request loses its valid signature.**
   `Selection.Manifest` was a `json.RawMessage`; saving the operation or install
   request with `json.MarshalIndent` changes its whitespace and strips the signed
   trailing newline. The release tool signs those exact bytes. `Apply` then fails
   signature verification before downloading. Store the signed manifest as opaque
   bytes (base64 within internal JSON) so both persistence steps preserve it.
2. **That early failure leaves “downloading” forever.** Request verification
   returned before the failure-status handler was installed. Early failures now
   write an error status when there is no active updater lock.
3. **Update service latency freezes the touchscreen.** Opening settings, checking,
   and confirming installation performed blocking socket calls on the LVGL thread.
   The baseline regression linked the original settings implementation from HEAD
   against the host UI libraries: a 250 ms simulated request blocked the event
   handler for 250 ms and failed the responsiveness check. The fixed implementation
   dispatches callbacks to one worker and consumes results on the UI thread.
   Each handler returned in under 1 ms in the local run. The original socket read
   timeout is 20 seconds, so a slow check could stall the original UI much longer.
4. **No installation progress refresh.** The touchscreen never requested status
   again after starting installation. It now polls once per second during an active
   update, retries a temporary manager disconnect, and displays terminal errors.
5. **Concurrent operations overwrite active installation status.** A check could
   replace downloading/validating with checking/idle. Checks now reject all active
   installation phases. A rejected second installer also no longer writes failure
   over the lock owner's status.

The local socket also uses a send timeout and suppresses SIGPIPE if Manager exits
while the touchscreen sends a request.

## Validation

- Full `services/managerd` Go test suite passes.
- Update, server, and bundle-tool tests pass with the Go race detector.
  The update package has 71.6% statement coverage; this is not a claim of exhaustive
  coverage or complete emulation of the device.
- Real installer filesystem and transaction paths tested: successful signed install
  with the production ten-second health stability requirement; failed health checks
  rolling back to factory and an existing release; service-start failure; cancelled
  and corrupt downloads; request persistence across a manager restart; bad signatures;
  duplicate operations; launch failure; wrong version and reflash-required rejection.
- Download failure matrix: HTTP 503, truncation, excess bytes, wrong SHA-256,
  cancellation, and maximum bundle size rejection, with no partial-file leak.
- Existing recovery tests pass for stale locks, pre-activation interruption,
  activation before its phase was recorded, unconfirmed factory updates,
  interrupted rollback, confirmed transactions, and release retention.
- Existing archive containment, manifest validation, signed release discovery,
  bundle generation/verification, and loopback authorization tests pass.
- `pedal-lvgl-ui-smoke` and `pedal-ota-ui-regression` pass. The new headless regression
  exercises slow callbacks, duplicate presses, install confirmation, status polling,
  temporary manager disconnection, visible failure, and navigation with a request
  outstanding.
- The host pedal application builds successfully. Linux arm64 updater and manager
  daemon cross-compilation and targeted Go vet pass.

Re-run from the repository root:

```sh
(cd services/managerd && go test ./... -count=1)
(cd services/managerd && go test -race ./internal/update ./internal/server ./cmd/ardor-ota-tool -count=1)
cmake --build build-lvgl-test --target pedal-ota-ui-regression pedal-lvgl-ui-smoke -j 6
ctest --test-dir build-lvgl-test -R 'pedal-(ota-ui-regression|lvgl-ui-smoke)' --output-on-failure
```

Host CMake configuration needs the repository's pinned LVGL/miniaudio/NAM sources
(including the Eigen submodule) and FFTW development headers/library. This workspace
had stale source overrides pointing at a deleted build-sdl directory; local cache
paths were repaired for these runs without changing project dependency pins.

## Deployment and remaining device checks

Updater version is now **1.0.1**, and newly generated bundles require that version.
The manifest representation is private manager/updater state; the public status API
and signed release manifest format are unchanged. Older cached selections should be
checked again. An image with the original immutable `/usr/bin/ardor-updater` needs
an updated base image (or an explicit updater replacement) before these fixes can
work: application bundles contain only the pedal and manager binaries. This work
has not deployed anything.

These tests reproduce software failure modes consistent with the reported frozen
screen; they cannot establish which one occurred on the actual pedal. The full
hardware test still needs to verify framebuffer/DRM reacquisition, touchscreen
input, ALSA ownership, relay muting, real Wi-Fi/GitHub transfers, disk exhaustion,
and real power interruption/durability. Service scripts and audio health were
simulated, and there was no full-system ARM emulation.
