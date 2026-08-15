# Ardor Whammy — VST3 / CLAP

A plugin wrapper around the pedal's Whammy mode, so the algorithm can be
auditioned in a DAW. This is a test harness, not a product: no UI, no factory
presets, no saved state beyond the host's own parameter automation.

The DSP is the pedal's own `pedal::WhammyMode`, compiled from the same source
files the device uses.

---

## Requirements

| | macOS | Windows |
|---|---|---|
| Compiler | Xcode Command Line Tools | Visual Studio 2022 (Desktop C++) or MSYS2 MinGW-w64 |
| Build tools | CMake 3.20+ | CMake 3.20+ |
| Other | — | Git (CMake fetches DPF on first configure) |

Both need a working `git`, because the build downloads
[DPF](https://github.com/DISTRHO/DPF) at a pinned commit the first time you
configure. After that it builds offline.

The plugin is **off by default**. Nothing here affects the pedal image or its
test suite unless you pass `-DARDOR_BUILD_WHAMMY_PLUGIN=ON`.

---

## Build — macOS

```sh
cmake -S . -B build-vst -DARDOR_BUILD_WHAMMY_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-vst --target ardor-whammy-vst3 ardor-whammy-clap -j8
```

Run both commands from the repository root. The bundles land in
`build-vst/bin/`.

For a universal binary on an Intel host as well as Apple silicon, add:

```sh
-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
```

### Install — macOS

```sh
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/CLAP
cp -R build-vst/bin/ardor-whammy.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R build-vst/bin/ardor-whammy.clap ~/Library/Audio/Plug-Ins/CLAP/
```

Then rescan plugins in the DAW.

macOS will refuse to load an unsigned bundle downloaded from elsewhere, but a
locally built one is fine. If Gatekeeper complains after you move it between
machines, clear the quarantine flag:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/ardor-whammy.vst3
```

---

## Build — Windows

### Visual Studio 2022

From a *Developer Command Prompt for VS 2022*, at the repository root:

```bat
cmake -S . -B build-vst -G "Visual Studio 17 2022" -A x64 -DARDOR_BUILD_WHAMMY_PLUGIN=ON
cmake --build build-vst --config Release --target ardor-whammy-vst3 ardor-whammy-clap
```

Note the `--config Release` — Visual Studio is a multi-configuration generator,
so the build type is chosen at build time rather than at configure time.

### MSYS2 / MinGW-w64

From an *MSYS2 MinGW 64-bit* shell:

```sh
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake git
cmake -S . -B build-vst -G "Ninja" -DARDOR_BUILD_WHAMMY_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-vst --target ardor-whammy-vst3 ardor-whammy-clap
```

Bundles land in `build-vst/bin/` either way. On Windows a VST3 "bundle" is a
directory named `ardor-whammy.vst3` containing
`Contents/x86_64-win/ardor-whammy.vst3`.

### Install — Windows

Copy the whole `.vst3` folder, not just the DLL inside it.

Per user (no admin rights needed):

```bat
xcopy /E /I /Y build-vst\bin\ardor-whammy.vst3 "%LOCALAPPDATA%\Programs\Common\VST3\ardor-whammy.vst3"
xcopy /E /I /Y build-vst\bin\ardor-whammy.clap "%LOCALAPPDATA%\Programs\Common\CLAP\ardor-whammy.clap"
```

System-wide (needs an administrator prompt):

```bat
xcopy /E /I /Y build-vst\bin\ardor-whammy.vst3 "C:\Program Files\Common Files\VST3\ardor-whammy.vst3"
xcopy /E /I /Y build-vst\bin\ardor-whammy.clap "C:\Program Files\Common Files\CLAP\ardor-whammy.clap"
```

Then rescan plugins in the DAW.

---

## Formats and hosts

| Format | Built | Hosts |
|---|---|---|
| VST3 | yes | Ableton Live, Reaper, Cubase, Studio One, Bitwig, FL Studio, Mixbus |
| CLAP | yes | Reaper, Bitwig, FL Studio, Studio One 7+ |
| AU | **no** | — Logic and GarageBand will not see this plugin |

There is no Audio Unit build. Adding one needs a different wrapper; ask if you
want it.

---

## Controls

| Control | Notes |
|---|---|
| **Pedal** | Heel at 0, toe at 1. The one to automate or bind to an expression pedal. |
| **Preset** | 19 discrete steps: ten Whammy modes, then nine Harmony modes. |
| **Glide** | How quickly the pitch follows the pedal, about 50 ms down to 1 ms. |
| **Harmony Level** | Level of the shifted voice against the dry note. Harmony presets only. |
| **Tone** | The pedal's shared tone control. |
| **Mix** | Dry/wet. The Whammy presets carry no dry internally, as on the original. |
| **Level** | Output gain. 0.5 is unity. |

Preset order, matching the hardware selector:

```
 1  2 Oct up          11  Oct dn / Oct up
 2  1 Oct up          12  5th dn / 4th dn
 3  5th up            13  4th dn / 3rd dn
 4  4th up            14  5th up / 7th up
 5  2nd dn            15  5th up / 6th up
 6  4th dn            16  4th up / 5th up
 7  5th dn            17  3rd up / 4th up
 8  1 Oct dn          18  Min 3rd up / 3rd up
 9  2 Oct dn          19  2nd up / 3rd up
10  Dive bomb
```

---

## Sample rate

The pedal runs at 48 kHz. The plugin runs at whatever the host provides — the
pitch ratio itself is rate independent — but it only matches the device exactly
at 48 kHz, because the grain length and the control cadence are counted in
samples rather than in seconds. **Set the DAW to 48 kHz when comparing against
hardware.**

---

## Relationship to the pedal code

The plugin drives `pedal::WhammyMode` directly rather than going through
`ardor::DaisyFxProcessor`. That class hard-rejects any host rate other than
48 kHz, so routing through it would give a plugin that silently passes audio
through on a 44.1 kHz project. The dry/wet crossfade and output level laws in
`WhammyPlugin.cpp` are copied from that class, so a given knob position means
the same thing in both.

---

## Troubleshooting

**The DAW does not list the plugin.** Confirm you copied the whole `.vst3`
directory rather than the binary inside it, then trigger a rescan. Some hosts
cache a failed scan and need the plugin cache cleared.

**Configure fails downloading DPF.** The first configure needs network access.
Check `git` is on `PATH`; behind a proxy, set `HTTPS_PROXY` before running
CMake.

**MSVC reports C++ standard errors.** The sources need C++20. The build sets
this on every generated target, so if you see this you are probably configuring
with an older Visual Studio than 2022.

**It builds but the pedal image stops building.** It should not — the plugin
lives behind `ARDOR_BUILD_WHAMMY_PLUGIN`, off by default, in its own build
directory. Keep `build-vst` separate from the pedal build directory.

---

## Verification status

Built and checked on macOS (arm64): both bundles carry the correct entry points,
and all 19 preset indices were measured playing the interval their name claims,
within 10 cents worst case.

**The Windows instructions above are untested.** The plugin sources contain no
platform-specific code and DPF supports both toolchains, so they should hold,
but nobody has run them yet. Please report back if something needs adjusting.
