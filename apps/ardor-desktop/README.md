# Ardor standalone desktop prototype

This host opens the existing LVGL interface and runs the shared Ardor engine
without terminal audio-device flags. The first target is macOS. The session and
window tests also run on Linux; Windows and full-chain plugins remain later
milestones in [the desktop plan](../../docs/desktop-app-plan.md).

Implemented:

- A dedicated application target, with `Ardor.app` metadata on Mac.
- Audio setup for input/output interface, guitar channel, and buffer size.
- Device identities persisted across restarts and resolved again when opening
  audio. Missing devices fail visibly rather than selecting a default microphone.
- Explicit Start/Stop. Startup always leaves monitoring stopped.
- A per-user library and four factory presets using only built-in algorithms.
- Background preset preparation, live effect controls, structural-edit rollback,
  preset saving/navigation, scene recall, and the muted tuner.
- Portable sequential split/WDW execution; no Linux CPU affinity or FIFO-worker
  requirements in the desktop host.
- Resizable window, desktop audio setup from the existing Setup button, keyboard
  focus, and protection for unsaved preset edits when closing.
- Mac audio permission request and a recovery message for denied permission.
- A macOS CI build, session/UI checks, and downloadable Apple Silicon beta disk image.

This is a prototype. No Mac hardware audio/latency, sleep/wake, or clean-machine
installation check has been performed from the Linux development environment.
The staged bundle is not yet a signed/notarized public release.
Desktop looper controls, MIDI input, file-picker asset import/export, an app icon,
and release signing/notarization are still to follow.

## Download a beta

Open a successful **Desktop beta** run in GitHub Actions and download its
`Ardor-macOS-arm64-beta-<commit>` artifact (GitHub sign-in is required).
The build summary links directly to the download. Unzip it, open the `.dmg`,
and drag Ardor to Applications. The download includes installation notes and a
SHA-256 checksum. Pull requests, relevant pushes to `main`, and manual workflow
runs produce betas; artifacts expire after 30 days.

Betas currently support Apple Silicon Macs running macOS 15 or newer. They are ad-hoc signed, without
Developer ID signing or notarization. If macOS blocks a trusted download, use
[Apple's app-specific Open Anyway instructions](https://support.apple.com/102445).
A successful CI build does not replace testing with a real audio interface.

## Build on Mac

Install the Xcode command-line tools, CMake, and SDL2 on the development machine.
For example, with Homebrew already installed:

```sh
brew install cmake sdl2
cmake -S . -B build-desktop \
  -DARDOR_UI_BACKEND=sdl \
  -DARDOR_BUILD_DESKTOP=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-desktop --target ardor-desktop --parallel 3
open build-desktop/apps/ardor-desktop/Ardor.app
```

Builds fetch the existing pinned DSP/LVGL dependencies. End users of a completed
release will not need those tools or a source checkout.

To stage the app and bundle its non-system dynamic dependencies:

```sh
cmake --install build-desktop \
  --prefix "$PWD/build-desktop/stage" --component desktop
```

The result is `build-desktop/stage/Ardor.app`. Staging uses CMake BundleUtilities
to copy runtime dependencies and adjust their install names. Homebrew may supply
SDL2 through `sdl2-compat`; the app additionally bundles its dynamically loaded
SDL3 runtime when available, with license notices. Signing must happen
after staging because changing dylib paths changes the signed code. For local
prototype use, ad-hoc sign the staged bundle before launching:

```sh
codesign --force --deep --sign - build-desktop/stage/Ardor.app
open build-desktop/stage/Ardor.app
```

Ad-hoc signing is for local testing. The public release pipeline still needs
Developer ID signing of nested code and the app, hardened runtime with
`entitlements.plist`, notarization, and a stapled distribution artifact.

## Play

Open **Audio setup**, select an input and output interface, choose the guitar
channel, and save. Prefer the same physical interface for both directions.
Configure it for native 48 kHz, then press **Start audio**. On Mac, approve the
audio-input permission prompt. If denied, enable Ardor in System Settings →
Privacy & Security → Microphone and retry.

**Clean** passes the input through the engine and safety limiter. **Tremolo**,
**Chorus**, and **Delay** demonstrate built-in processing without external assets.
Use Edit to change the chain and Save to persist it. A disconnected interface
stops audio while preserving the draft; reconnect/select it and start again.
The displayed buffer duration is not a measurement of total round-trip latency.

The library is created at:

- Mac: `~/Library/Application Support/Ardor/`
- Linux development: `$XDG_DATA_HOME/ardor`, or `~/.local/share/ardor/`

The prototype initializes `models`, `irs`, `reverb-irs`, `presets`, `loops`, and
`settings`. Existing presets are never replaced by factory initialization. For
development, `--data-root DIR` uses a separate library; importing assets through
a file picker is the next usability step. Assets currently need to be placed in
the appropriate library folders before launch, and IRs must be compatible with
the existing 48 kHz loader.

## Validation

```sh
cmake --build build-desktop --target \
  ardor-desktop-session-smoke pedal-preset-activation-smoke pedal-lvgl-ui-smoke \
  --parallel 3
ctest --test-dir build-desktop --output-on-failure \
  -R '^(ardor-desktop-session-smoke|ardor-desktop-window-smoke|pedal-preset-activation-smoke|pedal-lvgl-ui-smoke)$'
```

The session test covers first-run preservation, factory processing, settings
round-trip/validation, rejected-preset retention, structural rollback, unsaved
drafts during audio reconfiguration, saving, corrupt settings recovery, and
sequential WDW processing. It never opens an audio device.

The window smoke test is registered on Linux with SDL's dummy video/software
rendering drivers. It checks startup, audio setup validation, resize/hit-testing,
close cancellation, and clean shutdown. To capture its screens:

```sh
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy \
  ./build-desktop/apps/ardor-desktop/ardor-desktop \
  --smoke-test --smoke-screenshots /tmp/ardor-desktop-screens
```

Smoke tests use an isolated temporary library and remove it after exit.
