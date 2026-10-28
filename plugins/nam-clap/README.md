# Ardor NAM

A standalone CLAP effect for macOS Apple Silicon, using Ardor's actual NAM loader.
It supports snapshot NAMs, ConcatWaveNet / ConcatLSTM controls, FiLM schema 1,
embedded parametric payloads and compatible one-to-eight-tier containers.

Open a `.nam` file, adjust its modeled controls, and select Nano / Standard when
both are present. A single model stays a single model; the plugin does not invent
another size. Extra exported tiers are selectable individually. Switches use their
exported names. Continuous controls retain Ardor's smoothing. The host can automate
physical model controls, input/output trims, input channel and input calibration.
Size changes prepare a new instance and request a host audio restart; they are not
advertised as automatable. Controls and their automation IDs survive compatible
tier changes. Model loads and size changes never parse or prewarm on the audio thread.
Upstream channel-sliced WaveNets also expose their native widths as Size 1, Size 2,
and Full, using the runtime's own boundaries.

Saved CLAP state embeds the original model and controls for portable project recall.
Files are limited to 64 MiB and saved state to 128 MiB. A failed model load preserves
the previous model. The initial size of a combined file is Standard (the last tier).

The engine runs models at 48 kHz, with the same loudness normalization and optional
input-level mapping as Ardor. The streaming host-rate bridge handles other DAW rates
and reports its latency. Stereo tracks feed one mono model: choose Left, Right, or
Average. Output is duplicated to stereo. Bypass is faded and latency aligned; a fixed
-1 dBFS ceiling and non-finite checks protect the output. No cabinet IR or extra pedal
effects are added. Use qualified playback exports from the trainer where available.

## Build

```sh
cmake -S plugins/nam-clap -B build-nam-clap -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
cmake --build build-nam-clap --parallel 2
ctest --test-dir build-nam-clap --output-on-failure
cmake --install build-nam-clap --prefix stage-nam-clap
codesign --force --sign - stage-nam-clap/ArdorNAM.clap
```

Copy `ArdorNAM.clap` into `~/Library/Audio/Plug-Ins/CLAP/`, then rescan plugins in
Bitwig, REAPER or another CLAP host. This is a CLAP effect; Logic Pro needs an Audio
Unit and cannot open it directly. The macOS build has a native embedded Cocoa editor.
Linux builds are provided only for ABI/audio validation and use host parameter UI.
Resize the editor freely within 600–1400 × 600–1200 logical pixels. Extra height
goes to the model-control list; the header and bottom input/output section keep
their height. Wider windows lengthen sliders while labels, values and control
heights retain their size. The model list scrolls when needed.
Dependencies are pinned; builds fetch the same NAM core as Ardor, CLAP headers,
Eigen and SpeexDSP. Licenses are installed alongside the module.

The `NAM CLAP` GitHub Actions workflow produces `ArdorNAM-macOS-arm64.zip`,
including the plugin, this guide and license notices. It requires macOS 13 or
newer. These development builds are ad-hoc signed, rather than notarized with a
Developer ID. If macOS blocks a downloaded build, remove quarantine from this
specific installed bundle before rescanning:

```sh
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/CLAP/ArdorNAM.clap"
```

The workflow also runs model/reference inference, automation, project recall and
rate-conversion checks, then the upstream CLAP validator. Its native editor test
captures empty, single-model, combined-model, 24-control and minimum-size views
in the `ArdorNAM-checks-macos-26` artifact (with the commit appended to the name).

The calibration controls are optional. Enable them only when the model supplies
`input_level_dbu` and you know your interface's matching full-scale reference.
Uncalibrated operation leaves that mapping at unity and retains the manual input trim.
