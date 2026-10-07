# ERB-PS2 CPU prototype and POG3 applicability

Updated 2026-10-07. This is the next DSP experiment, not a production backend
selection. CPU feasibility remains the gate before public block integration.

## Evidence and scope

Etienne Thuillier's 2016 thesis,
[Real-Time Polyphonic Octave Doubling for the Guitar](https://aaltodoc.aalto.fi/items/6fd2cfc9-db61-4b0a-b11f-ea8c2925509f),
provides a concrete octave-up reference. Sections 5.1–5.2, 6.3–6.4, 7 and 8
describe its equations, band specification, evaluation and limitations. The full
73-page PDF was retrieved from the university's bitstream content endpoint and
its technical/evaluation sections inspected. The companion site provides
[reference audio](https://research.spa.aalto.fi/publications/theses/thuillier_mst/).

Its ERB-PS2 setup has 43 complex bands, qERB=6, qC=4, leftmost target ERB=5,
and target width=28 ERB. The experiment concerns octave doubling (ratio 2), with
approximately 160 Hz–8 kHz output coverage. It runs non-decimated filters in
offline blockwise Matlab simulations. Its roughly 3 ms response concerns high
output frequencies; low-band group delay/ringing is considerably longer.
Narrower bands reduce intermodulation at the expense of transient ringing. The
thesis reports a roughness spike and remaining distortion, and supplies no
measured embedded C++ CPU budget for our full effect.

The [Terrarium implementation](https://github.com/schult/terrarium-poly-octave/tree/bc7ddf35362cf4b0e9d46b77bc21e490f79f9ed1)
uses a related 80-band phase-scaling design at 8 kHz, with fixed down-two,
down-one and up-one outputs. Ardor already ports it under
`src/daisyfx/hosted/dsp/{band_shifter,octave_generator,multirate}.h`; the current
port uses 48 bands. Their logarithmic center grids and bandwidth rules differ
from the thesis specification. Neither implementation validates the exact
ERB-PS2 design, the fifth, continuous Warp, +24, or independent-note Attack.

## Reference prototype: reproduce octave-up before extending it

1. Add an isolated test/reference processor alongside the optional library
   trial. Leave the current POG3 processor and the existing Poly Octave mode
   untouched. Use fixed storage for two independent stereo channel histories.
2. Implement equations 37–40 for the band specification in double precision
   during preparation. Target centers use `z = 5 + k * 4/6`, for k=0…42;
   analysis centers are the target frequencies divided by 2. Analysis bandwidth
   follows `ERB(2*fc)/(2*6)`, with `ERB(f)=24.7+0.108*f`. Preserve those design
   values before tuning the bank for guitar.
3. Derive the complex minimum-phase filter from section 5.2.1 and verify its
   transfer response against an independent offline calculation. Do not label
   a Terrarium lowpass-prototype substitution an exact thesis reproduction.
   Resolve the coefficient transformation against table 5/figure 15 before
   taking conclusions about crossover attenuation or response latency.
4. Process at 48 kHz without decimation for this reference. Emit octave-up as
   the real part of `z*z/abs(z)`, preserving magnitude. Handle exact silence
   explicitly. Start with accurate magnitude evaluation and ordinary floating
   point rules; approximate reciprocal roots are a later measured tradeoff.
5. Measure the bank response across band centers and crossovers. Keep raw gain
   results, then specify any fixed output calibration explicitly. Do not use
   per-fixture normalization to hide pitch-dependent cancellation or loss.
6. Exercise settled tones, a logarithmic sine sweep, two crossing tones,
   resolved and close low chords, impulse/plucked transients, silence and reset.
   Compare tuning, amplitude, generated partials, roughness/beating, and
   frequency-dependent onset/decay. Use the companion examples for listening
   context without treating our different platform as a reproduction of pedal
   recordings.
7. Verify arbitrary callback partitions at 64/128 samples, finite stereo
   output, repeatable reset, and allocation/free counts. Report measured CPU
   mean, p99, maximum and overruns separately from audio metrics. All buffers,
   coefficients and gain tables belong to preparation.

## Extend the reference into a shared multi-voice prototype

Proceed only if the reference justifies the added work with measured CPU and
sound quality. A single octave-up result is insufficient to replace POG3.

1. Keep one analytic filter state per band/channel and separate phase/output
   histories per voice. Reuse its magnitude across voices. Integer octave-up
   branches can square the unit complex carrier successively for +12/+24.
   Downward roots need persistent branch selection, especially through silence,
   near-zero magnitude, and sign/phase crossings.
2. Treat the equal-tempered fifth as ratio `2^(7/12)`. A fixed center-frequency
   translation does not provide exact transposition for a tone between centers.
   Validate true carrier phase increments and double phase histories before
   introducing this voice or advertising generic pitch shifting.
3. For moving Warp, integrate the desired output phase increment from the
   observed input increment and ratio. Multiplying an ever-growing phase by a
   changing ratio introduces an unwanted term from ratio changes. Test ramp,
   reversal, and endpoint behavior against the current Warp contract, including
   fixed long upper paths and shifted short Focus paths where applicable.
4. Revisit analysis coverage and bandwidth for the full ratio range. The
   thesis's target range is specified for ratio 2; blindly sharing that range
   between .25 and 4 omits required source frequencies or worsens roughness.
   Evaluate whether one conservative bank or several rate/band tiers offers
   the best total cost. Count every warm branch during transitions.
5. Evaluate alias rejection at each upward ratio. Masking only bands whose
   centers exceed Nyquist is insufficient proof: their finite-width tails and
   modulation products also matter. Oversampling or selective-rate processing,
   if necessary, must enter both CPU and memory accounting.
6. Preserve separate voices through level, Attack, filter, detune and pan stages.
   The existing Terrarium path mixes voices before interpolation and uses a
   broadband swell. It cannot implement our independent-note Attack behavior
   unchanged. A band envelope also does not isolate two notes in one band.
7. Prototype per-note ownership/envelope mapping and held carrier capture only
   after the pitch bank is viable. Include their CPU cost and shared analysis
   work in the full comparison. Freeze/gliss needs persistent identities,
   capture/release history and assignment behavior; storing a band amplitude
   alone does not establish those contracts.

## CPU decisions

First measure one stereo octave-up reference, then the full five shifted voices
and processed dry, then Attack and freeze/gliss. Record each added cost. Filter
counts alone are misleading: 43 bands at 48 kHz execute about 3.2 times as many
band updates as 80 bands at 8 kHz, before accounting for different modulator
work, stereo, and additional voices. This is an operation-count observation,
not a prediction of measured processor demand.

Use ordinary Release flags, an immutable FFTW POG3 control, identical input and
control timelines, and sequential opposite-order timing runs after builds and
quality/allocation checks finish. Retain outliers. Adopt a backend only after
full-path CPU meets the goal with acceptable tails and every required DSP
behavior survives. Then verify AArch64 execution, complete memory footprint,
combined-chain endurance and listening. Keep M8 blocked until admission.
