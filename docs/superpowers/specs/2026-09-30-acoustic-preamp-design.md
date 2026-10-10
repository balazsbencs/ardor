# Acoustic EQ — Draft Design

Date: 2026-09-30
Updated: 2026-10-02
Status: Draft for discussion; implementation has not started.

## Goal and confirmed direction

Make the K&K Pure Mini sound **natural and woody with light correction**. Keep
the guitar's attack, body and playing dynamics intact. The block should be
quick to dial in and able to control boom or feedback when the room demands it.
Grace Design ALiX is one useful reference. The target is the sound and stage
workflow of this pickup and guitar.

The main effect is an Acoustic EQ mode. The player may also need appropriate
analog input loading, gain staging, mute and a solo lift, but those belong to
the surrounding rig. Compression and microphone-like body imaging may be
explored later if listening tests show that EQ alone leaves a specific gap.

## Sound and control priorities

1. **A restrained starting sound.** Begin with a gentle high-pass filter and
   otherwise flat EQ. Preserve the natural Pure Mini sound at the correct input
   load; add tonal changes only when listening identifies a repeatable need.
2. **Musical adjustment.** Independent bass and treble shelves plus two
   overlapping parametric mids: one for body/boxiness and one for presence or
   attack. These names guide the player but do not constrain the exact center
   frequencies.
3. **Live correction.** An independent high-pass filter and narrow, manually
   sweepable notch can run together when a room or monitor creates a specific
   problem. Keep the notch off in normal use.
4. **Clean operation.** No intentional saturation, artificial noise or added
   buffering delay. Level-match listening comparisons before claiming an
   improvement.

These are proposed starting priorities, to be refined by listening with the
actual guitar and pickup. The ALiX's shared HPF/Notch switch and two-range
mid control are not requirements for this design.

## K&K Pure Mini and the analog input

A DSP block operates after the ADC. It cannot change the pickup's physical
load or replace an analog gain stage. Loading affects the signal before
conversion; digital EQ is not a substitute for correctly buffering the pickup.

K&K's [official troubleshooting guidance](https://www.kksound.com/help-troubleshooting)
recommends 500 kΩ to 1 MΩ for the Pure pickup family and explains that 5–10 MΩ
can exaggerate its bass response. Its [Pure Preamp](https://www.kksound.com/pure-preamp)
uses a 1 MΩ input. The working input target for this pickup is therefore
about 1 MΩ.

The repository has several input board designs. The control-IO board documents
a nominal 1 MΩ load in `hardware/control-io/README.md`. The standalone buffer
also contains 1 MΩ resistors, but its assembled input impedance must be checked
from the full circuit and actual build rather than inferred from its BOM.

The installed board is still unconfirmed. If it provides the documented 1 MΩ
input, there is no loading-based reason yet to redesign it for this pickup.
Verify its actual load, frequency response, noise and ADC headroom with the
Pure Mini before deciding whether any hardware work is necessary. Do not
expose a fictitious software impedance switch.

## Block identity and processing

Display name: **Acoustic EQ**. Proposed identity: `type: "eq"`,
`mode: "acoustic"`, in the existing utility category. The new mode gives a
focused editing surface while preserving existing Five Band EQ presets.
Ardor currently assumes `eq` means `parametric_eq_5` in several paths, so
each requires a mode-aware update.

The existing Five Band EQ is an excellent prototype: it already has a HPF and
five peaking bands. It does not have a dedicated narrow notch or low/high
shelves. Start listening experiments with it before implementing the new mode,
then build only the controls whose value is audible and repeatable.

Proposed signal path:

```text
Ardor input gain → Acoustic EQ → other chain effects → scene trim → output
                   [ HPF → notch → bass → low mid → high mid → treble ]
```

This is a proposed digital signal path. The HPF and notch have independent
enable switches. Static linear EQ stages commute; the exact ordering matters
if nonlinear processing is added later.

Process stereo with identical controls and independent channel states, so the
block also works inside Dual Rig lanes or after stereo effects. Normal acoustic
use starts with Ardor's mono input. Add no buffering delay, oversampling,
artificial distortion or noise. IIR filters retain their normal phase response
and group delay.

Reuse the coefficient definitions, response evaluation, peaking EQ, high-pass
and high-shelf helpers from `src/equalizer/ParametricEqMath.{h,cpp}` where their
transfer functions fit. Add a low-shelf helper and a notch realization as
needed. Use a dedicated processor under `src/equalizer/`, with parameter
validation, preparation, live targets, block processing and reset.

Compute coefficients at a bounded control cadence, not per sample. Smooth
EQ gain in dB and frequency logarithmically. Keep the audio callback allocation
and lock free. Give filter enable/disable and large frequency moves brief,
tested transitions so live adjustment does not click.

## Voicing method

Build a small, repeatable listening set with the actual
pickup: gentle fingerstyle, hard strumming, low notes and percussive playing.
Capture a dry DI with measured input loading and safe ADC headroom. If
possible, record a microphone reference from the same performance to identify
what listeners miss in the pickup sound. Use level-matched A/B tests so a
louder setting does not win automatically.

First audition the current Five Band EQ to locate frequent problem ranges.
Then tune the proposed shelves and mid bands, starting with broad low-gain
corrections. Test the notch on a real monitor or controlled feedback setup.
The initial 1 MΩ loading is important: compensating for a mismatched input in
DSP would produce a voicing specific to that hardware mistake.

Use a neutral default: proposed 40 Hz, 12 dB/octave high-pass; notch off; all
shelf and mid gains at 0 dB; output trim at unity. This cutoff is a starting
value to validate with the actual guitar, not an ALiX setting. Offer any more
colored voicings as optional presets only after listening. Avoid claiming a
universal Pure Mini curve: guitar body, pickup installation and PA all affect
the result. Keep every preset editable.

## Controls and storage proposal

Use semantic keys and physical units for the Acoustic EQ mode. These are
working ranges for prototyping, not product specs:

| Control | Prototype range | Default intent |
| --- | --- | --- |
| HPF | Off or 20–250 Hz, 12 dB/octave | 40 Hz starting point |
| Notch | Off or 50–1,000 Hz, narrow cut | Off until feedback needs it |
| Bass shelf | Approximately 80–250 Hz, ±12 dB | Flat |
| Low mid | Approximately 100–1,200 Hz, ±12 dB, adjustable Q | Flat |
| High mid | Approximately 700 Hz–6 kHz, ±12 dB, adjustable Q | Flat |
| Treble shelf | Approximately 2–10 kHz, ±12 dB | Flat |
| Output trim | Enough range to level-match EQ settings | Unity |

The EQ controls should use logarithmic frequency movement, readable physical
units and sensible encoder steps. More overlap than the table suggests may be
useful; refine ranges by listening and checking stability at 48 kHz. The
proposed 40 Hz HPF cutoff needs confirmation from bass response measurements
and playing tests. It will alter the signal slightly even with every EQ gain
at zero.

Keep the device's first view focused on HPF, bass, body, presence and treble.
Put band frequencies/Q and notch settings in a deeper edit view; the Manager
can show the complete response and controls together. The notch should still
be quick to reach during a sound check.

## Stage controls and routing

Keep input gain, tuner and mute in Ardor's existing rig controls. The Acoustic
EQ output trim only matches level after equalization. Use scene post-rig trim
for a solo lift after other effects. If polarity inversion improves a real
stage setup, add it as a reusable routing control rather than an EQ parameter.
Input impedance, analog gain, DI isolation and physical output routing remain
hardware or rig concerns.

## Integration plan

1. Identify the installed input board and capture/listen to Pure Mini DI
   recordings using the current Five Band EQ. Settle the control set and
   starter voicings from those experiments.
2. Implement parameter handling and the DSP processor, then connect preparation
   and processing through `ChainPlan`, engine construction and `RuntimeChain`.
   Cover serial, Dual Rig and wet/dry/wet placements.
3. Add defaults, controls and setters to the device model, Manager catalog,
   runtime commands and preset validation. The existing five-band EQ editor
   assumes a different schema, so give Acoustic EQ its own controls and reuse
   the response graph math where possible. Regenerate the website effect data
   and update catalog-dependent copy when the block actually ships.
4. Qualify block bypass and continuous parameters for scenes/MIDI.
   Scene support is an explicit capability registration, not something a new
   mode inherits automatically. Keep HPF/notch enable switches shared across
   scenes until their transitions are separately qualified. Booleans need an
   explicit mapping contract.
5. Verify parameter parity across device, Manager and saved presets; add an
   asset-free acoustic example and level-matched listening comparisons.

## Validation and acceptance

- Measure the magnitude and phase response of each stage and combined settings.
  Check that control values and units describe the measured response.
- Check the mid ranges, shelf boost/cut curves, HPF slope, and notch
  bandwidth/depth. Verify that HPF and notch work simultaneously.
- Sweep parameters and switch modes under signal; assert finite, stable output
  and measure transition artifacts, including high-Q low-frequency cases.
- Verify independent stereo state, silence/denormal handling, reset and bypass.
  Use the actual default HPF response rather than an invalid unity-null test.
- Verify preset round trips, invalid-value handling, nested routing, live
  updates and each qualified scene/MIDI behavior.
- Benchmark Pi 4 processing at 48 kHz and block sizes 64/128. Report added
  cost and worst callback timing; low CPU cost is an expectation until measured.
- Run level-matched listening tests using the Pure Mini source, including
  dynamics, low notes and a live monitor setup for manual feedback adjustment.
  Keep corrections that consistently improve the result across performances
  without making the guitar sound processed. If the existing Five Band EQ
  achieves the target, a new mode has no demonstrated sonic benefit.

## Open decisions

- Installed input board and confirmation of its nominal 1 MΩ load.
- Availability of dry Pure Mini recordings and, ideally, a microphone reference
  of the same performance.
- Whether a dedicated polarity utility or footswitch action for scene post-rig
  trim is needed for the desired stage workflow.
