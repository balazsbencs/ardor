Hosted Daisy multi-fx slice
===========================

Source: `/Users/bbalazs/daisy/multi-fx`
Import date: 2026-07-10

This folder contains an unchanged portable subset of the Daisy multi-fx source
used by Ardor's hosted effect blocks. Keep copied source files functionally
unchanged when possible; put host adaptation in Ardor-owned wrappers or
`compat/`.

The hosted source now includes every portable modulation, delay, and reverb
mode from the upstream project, together with their portable DSP and parameter
dependencies. It deliberately excludes hardware audio, display, controls,
MIDI, QSPI preset storage, tempo, and application code.

`Poly Octave` uses a small header-only subset of Cycfi Q, vendored under
`third_party/cycfi-q/` with its MIT license and the preserved license notice in
the included fast-math header.

Originally copied first-slice files:

- `audio/stereo_frame.h`
- `config/constants.h`
- `config/delay_mode_id.h`
- `config/mod_mode_id.h`
- `config/reverb_mode_id.h`
- `params/param_range.h`
- `params/delay_param_id.h`
- `params/delay_param_set.h`
- `params/delay_param_set.cpp`
- `params/delay_param_map.h`
- `params/delay_param_map.cpp`
- `params/mod_param_id.h`
- `params/mod_param_set.h`
- `params/mod_param_set.cpp`
- `params/mod_param_map.h`
- `params/mod_param_map.cpp`
- `params/reverb_param_id.h`
- `params/reverb_param_set.h`
- `params/reverb_param_set.cpp`
- `params/reverb_param_map.h`
- `params/reverb_param_map.cpp`
- `dsp/allpass.h`
- `dsp/comb_filter.h`
- `dsp/dc_blocker.h`
- `dsp/delay_line_sdram.h`
- `dsp/delay_line_sdram.cpp`
- `dsp/diffuser.h`
- `dsp/diffuser.cpp`
- `dsp/early_reflections.h`
- `dsp/fast_math.h`
- `dsp/fdn.h`
- `dsp/fdn.cpp`
- `dsp/feedback_limiter.h`
- `dsp/lfo.h`
- `dsp/lfo.cpp`
- `dsp/tone_filter.h`
- `dsp/tone_filter.cpp`
- `modes/delay_mode.h`
- `modes/digital_delay.h`
- `modes/digital_delay.cpp`
- `modes/mod_mode.h`
- `modes/reverb_mode.h`
- `modes/room_reverb.h`
- `modes/room_reverb.cpp`
- `modes/vintage_trem_mode.h`
- `modes/vintage_trem_mode.cpp`
