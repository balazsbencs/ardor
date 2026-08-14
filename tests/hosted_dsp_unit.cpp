#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/hosted/dsp/delay_line_sdram.h"
#include "daisyfx/hosted/dsp/delay_tap_transition.h"
#include "daisyfx/hosted/dsp/fast_math.h"
#include "daisyfx/hosted/dsp/fdn.h"
#include "daisyfx/hosted/dsp/halfband_resampler.h"
#include "daisyfx/hosted/dsp/pitch_shifter.h"
#include "daisyfx/hosted/dsp/tone_filter.h"
#include "daisyfx/hosted/modes/poly_octave_mode.h"
#include "daisyfx/hosted/modes/rotary_mode.h"
#include "daisyfx/hosted/modes/whammy_mode.h"
#include "daisyfx/hosted/params/reverb_param_map.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireFinite(float value, const std::string& message)
{
  require(std::isfinite(value), message);
}

void verifyToneFilter(float sampleRate)
{
  pedal::ToneFilter tone;
  tone.Init(sampleRate);

  for (int i = 0; i <= 1000; ++i) {
    const float knob = static_cast<float>(i) / 1000.0f;
    tone.SetKnob(knob);
    for (int sample = 0; sample < 1024; ++sample) {
      const float input = sample == 0 ? 1.0f : ((sample & 1) == 0 ? 0.25f : -0.25f);
      requireFinite(tone.Process(input), "ToneFilter sweep must remain finite");
    }
  }

  tone.SetKnob(0.5f);
  const float inputs[] = {-1.0f, -0.25f, 0.0f, 0.125f, 1.0f};
  for (const float input : inputs) {
    require(tone.Process(input) == input, "ToneFilter centre must be a true bypass");
  }

  tone.SetKnob(std::numeric_limits<float>::quiet_NaN());
  require(tone.Process(0.25f) == 0.25f, "non-finite ToneFilter knob must fall back to centre");
  require(tone.Process(std::numeric_limits<float>::infinity()) == 0.0f,
          "non-finite ToneFilter input must be contained");
  require(tone.Process(0.0f) == 0.0f, "ToneFilter must recover after non-finite input");

  tone.SetKnob(0.0f);
  (void)tone.Process(1.0f);
  tone.Reset();
  require(tone.Process(0.0f) == 0.0f, "ToneFilter reset must clear state");
}

void verifyHalfbandResamplers()
{
  pedal::HalfbandDecimator2x decimator;
  float output = 0.0f;
  for (int sample = 0; sample < 256; ++sample) {
    if (decimator.Push(1.0f, output)) {
      requireFinite(output, "halfband decimator output must remain finite");
    }
  }
  require(std::fabs(output - 1.0f) < 0.001f, "halfband decimator must preserve DC gain");

  pedal::HalfbandInterpolator2x interpolator;
  std::array<float, 2> upsampled{};
  for (int sample = 0; sample < 256; ++sample) {
    upsampled = interpolator.Process(1.0f);
    requireFinite(upsampled[0], "halfband interpolator output must remain finite");
    requireFinite(upsampled[1], "halfband interpolator output must remain finite");
  }
  require(std::fabs(upsampled[0] - 1.0f) < 0.003f && std::fabs(upsampled[1] - 1.0f) < 0.003f,
          "halfband interpolator must preserve DC gain");

  // DC gain alone would not catch a tap-alignment error in the polyphase
  // form, so also check passband flatness through a full decimate/interpolate
  // round trip. 1 kHz sits well inside the 9 kHz passband.
  {
    constexpr float kTwoPi = 6.28318530718f;
    pedal::HalfbandDecimator2x down;
    pedal::HalfbandInterpolator2x up;
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    std::array<float, 2> pending{};
    std::size_t pendingCount = 0;
    for (int sample = 0; sample < 20000; ++sample) {
      const float input = std::sin(kTwoPi * 1000.0f * static_cast<float>(sample) / 48000.0f);
      float held = 0.0f;
      float out = 0.0f;
      if (pendingCount != 0) {
        out = pending[2 - pendingCount];
        --pendingCount;
      }
      if (down.Push(input, held)) {
        pending = up.Process(held);
        pendingCount = 2;
        out = pending[0];
        pendingCount = 1;
      }
      if (sample >= 4000) {
        inputEnergy += static_cast<double>(input) * input;
        outputEnergy += static_cast<double>(out) * out;
      }
    }
    const double gain = std::sqrt(outputEnergy / inputEnergy);
    require(gain > 0.95 && gain < 1.05,
            "halfband round trip must be flat at 1 kHz within 0.5 dB");
  }

  decimator.Reset();
  interpolator.Reset();
  std::array<float, 2> cached{};
  std::size_t cachedCount = 0;
  std::size_t cachedIndex = 0;
  float peak = 0.0f;
  std::size_t peakFrame = 0;
  for (std::size_t frame = 0; frame < 96; ++frame) {
    float sample = 0.0f;
    if (cachedCount != 0U) {
      sample = cached[cachedIndex++];
      --cachedCount;
    }
    float downsampled = 0.0f;
    if (decimator.Push(frame == 0U ? 1.0f : 0.0f, downsampled)) {
      cached = interpolator.Process(downsampled);
      cachedIndex = 0;
      cachedCount = cached.size();
    }
    if (std::fabs(sample) > peak) {
      peak = std::fabs(sample);
      peakFrame = frame;
    }
  }
  require(peakFrame == 31U, "halfband adapter latency must match the dry delay");
}

void verifyFastSineAccuracy()
{
  constexpr float kTwoPi = 6.28318530718f;
  float maxError = 0.0f;
  for (int index = 0; index <= 10000; ++index) {
    const float phase = kTwoPi * static_cast<float>(index) / 10000.0f;
    const float approximation = pedal::fast_sin(phase);
    require(approximation >= -1.0f && approximation <= 1.0f,
            "fast sine must not overshoot its normalized range");
    maxError = std::max(maxError, std::fabs(approximation - std::sin(phase)));
  }
  require(maxError < 0.0002f, "fast sine must meet the modulation accuracy target");
}

void verifyBrightReverbs()
{
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    if (descriptor.blockType != "reverb") continue;

    ardor::DaisyFxProcessor processor;
    std::string error;
    auto params = ardor::defaultDaisyFxParams(descriptor);
    params["mix"] = 1.0f;
    params["tone"] = 1.0f;
    require(processor.configure(descriptor.blockType, params, 48000.0f, error), error);

    for (int sample = 0; sample < 96000; ++sample) {
      const float input = sample == 0 ? 1.0f : 0.0f;
      const auto output = processor.process({input, input});
      requireFinite(output.left, descriptor.mode + " bright reverb left must remain finite");
      requireFinite(output.right, descriptor.mode + " bright reverb right must remain finite");
    }
  }
}

void verifyDelayLineReset()
{
  std::array<float, 64> buffer{};
  pedal::DelayLineSdram line;
  line.Init(buffer.data(), buffer.size());
  line.SetDelay(17.0f);
  line.Reset();

  for (int sample = 0; sample < 32; ++sample) {
    const float output = line.Read();
    if (sample == 17) {
      require(output == 1.0f, "DelayLine reset must retain the configured delay");
    } else {
      require(output == 0.0f, "DelayLine reset must clear prior audio history");
    }
    line.Write(sample == 0 ? 1.0f : 0.0f);
  }
}

void verifyHighQualityDelayTap()
{
  constexpr float kTwoPi = 6.28318530718f;
  constexpr float kFrequency = 20000.0f;
  std::array<float, 65536> buffer{};
  pedal::DelayLineSdram line;
  line.Init(buffer.data(), buffer.size());
  double inputEnergy = 0.0;
  double outputEnergy = 0.0;
  for (int sample = 0; sample < 40000; ++sample) {
    const float input = std::sin(kTwoPi * kFrequency * static_cast<float>(sample) / 48000.0f);
    const float output = line.ReadAtHighQuality(100.5f);
    line.Write(input);
    if (sample >= 8000) {
      inputEnergy += static_cast<double>(input) * input;
      outputEnergy += static_cast<double>(output) * output;
    }
  }
  const double gain = std::sqrt(outputEnergy / inputEnergy);
  require(gain > 0.78, "band-limited delay tap must retain 20 kHz within about 2 dB");
}

// Measures the magnitude a fractional tap retains at `frequency`, driving the
// line with a sine and comparing settled output energy to input energy.
double fractionalTapGain(float frequency, float sampleRate, float delaySamples,
                         bool highQuality)
{
  constexpr float kTwoPi = 6.28318530718f;
  std::array<float, 8192> buffer{};
  pedal::DelayLineSdram line;
  line.Init(buffer.data(), buffer.size());
  double inputEnergy = 0.0;
  double outputEnergy = 0.0;
  for (int sample = 0; sample < 40000; ++sample) {
    const float input = std::sin(kTwoPi * frequency * static_cast<float>(sample) / sampleRate);
    const float output = highQuality ? line.ReadAtHighQuality(delaySamples)
                                     : line.ReadLinear(delaySamples);
    line.Write(input);
    if (sample >= 8000) {
      inputEnergy += static_cast<double>(input) * input;
      outputEnergy += static_cast<double>(output) * output;
    }
  }
  return std::sqrt(outputEnergy / inputEnergy);
}

// The reverb tank runs at 24 kHz, so a moving tap read with linear
// interpolation loses roughly 2 dB at 5 kHz on every pass. Inside a feedback
// loop that compounds. The band-limited reader must be effectively flat.
void verifyTankRateInterpolation()
{
  const double linear = fractionalTapGain(5000.0f, 24000.0f, 100.5f, false);
  const double banded = fractionalTapGain(5000.0f, 24000.0f, 100.5f, true);

  require(linear < 0.85,
          "linear interpolation is expected to lose >1.4 dB at 5 kHz / 24 kHz");
  require(banded > 0.99,
          "band-limited tap must hold 5 kHz within 0.1 dB at the tank rate");
}

// A modulated FDN reads a moving fractional tap on every circulation. The tail
// must not lose its high frequencies relative to an unmodulated tank.
void verifyModulatedFdnRetainsHighFrequencies()
{
  constexpr float kTwoPi = 6.28318530718f;
  constexpr int kLines = 4;
  std::array<std::array<float, 4099>, kLines> buffers{};
  constexpr std::array<std::size_t, kLines> delays{1361, 1657, 1949, 2273};

  const auto tailEnergy = [&](float modulationDepth) {
    pedal::Fdn::Config config{};
    config.n_lines = kLines;
    config.sample_rate = 24000.0f;
    for (int line = 0; line < kLines; ++line) {
      config.bufs[line] = buffers[static_cast<std::size_t>(line)].data();
      config.delays[line] = delays[static_cast<std::size_t>(line)];
      config.buffer_sizes[line] = buffers[static_cast<std::size_t>(line)].size();
    }
    pedal::Fdn fdn;
    fdn.Init(config);
    fdn.SetDecay(2.0f);
    fdn.SetDamping(1.0f);  // no damping, so any loss comes from interpolation
    fdn.SetModulation(modulationDepth);

    double energy = 0.0;
    for (int sample = 0; sample < 48000; ++sample) {
      if ((sample % 48) == 0) fdn.PrepareBlock();
      // Excite with 5 kHz for the first 100 ms, then measure the decaying tail.
      const float drive = sample < 2400
          ? std::sin(kTwoPi * 5000.0f * static_cast<float>(sample) / 24000.0f)
          : 0.0f;
      const auto out = fdn.Process({drive, drive});
      if (sample >= 12000) {
        energy += static_cast<double>(out.left) * out.left +
                  static_cast<double>(out.right) * out.right;
      }
    }
    return energy;
  };

  const double still = tailEnergy(0.0f);
  const double moving = tailEnergy(8.0f);
  require(still > 0.0, "unmodulated tank must produce a 5 kHz tail");
  // Turning modulation on must not cost high-frequency energy. With linear
  // interpolation this ratio measured 0.34 (-4.6 dB); with the band-limited
  // reader it measures 2.39 (+3.8 dB), because modulation now only spreads
  // energy across modes instead of also attenuating it.
  const double ratio = moving / still;
  require(ratio > 0.9,
          "modulating the FDN must not attenuate the 5 kHz tail");
}

// Rotary splits the signal into a drum band and a horn band. Those bands must
// reconstruct the input. Taking lp() and hp() from one SVF does not: the pair
// satisfies LP + HP = input - k*BP, which nulls completely at the crossover.
// Drive the real mode with sines either side of the crossover and require that
// none of them disappears.
void verifyRotaryCrossoverHasNoNull()
{
  constexpr float kTwoPi = 6.28318530718f;
  auto params = pedal::mod_fx::ParamSet::make_default();
  params.depth = 0.0f;   // no Doppler or AM, so only the crossover shapes the output
  params.p1 = 0.0f;      // no drive
  params.p2 = 0.0f;      // chorale
  params.tone = 0.2f;    // crossover lands at 500 + 0.2*1500 = 800 Hz

  // Hold the probe tone fixed and sweep Tone, which moves the crossover across
  // 500-2000 Hz. This isolates the crossover from the fixed horn/drum path
  // delay difference, which combs the response by a couple of dB by design.
  // With the broken split, the output collapsed as the crossover passed the
  // probe frequency. It must now stay live at every Tone setting.
  constexpr float kProbeHz = 800.0f;
  double worstGain = 1.0e9;
  float worstTone = 0.0f;
  for (int step = 0; step <= 10; ++step) {
    params.tone = static_cast<float>(step) / 10.0f;
    pedal::RotaryMode rotary;
    rotary.Init();
    rotary.Prepare(params);
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    for (int sample = 0; sample < 24000; ++sample) {
      if ((sample % 48) == 0) rotary.Prepare(params);
      const float input = std::sin(kTwoPi * kProbeHz * static_cast<float>(sample) / 48000.0f);
      const auto out = rotary.Process({input, input}, params);
      if (sample >= 8000) {
        inputEnergy += static_cast<double>(input) * input;
        const float mono = 0.5f * (out.left + out.right);
        outputEnergy += static_cast<double>(mono) * mono;
      }
    }
    const double gain = std::sqrt(outputEnergy / inputEnergy);
    if (gain < worstGain) {
      worstGain = gain;
      worstTone = params.tone;
    }
  }
  // The broken lp()+hp() split measured -72 dB once the crossover reached the
  // probe tone. -12 dB is far above that and well below the normal -2 to -6 dB.
  require(worstGain > 0.25,
          "rotary crossover must not null; worst gain at tone=" +
              std::to_string(worstTone));
}

// Poly Octave's 6x resamplers use cycfi ring buffers whose std::array storage
// is NOT zeroed by init_store(). Left unreset they emit uninitialised memory
// through the FIR taps for the first ~25 samples, which measured as high as
// 4.6e33 and then rang on in the downstream IIR shelves — sometimes leaving the
// mode silent. The output must be bounded from the very first sample, and must
// not depend on what happened to be in memory.
void verifyPolyOctaveStartsClean()
{
  constexpr float kTwoPi = 6.28318530718f;

  const auto run = [](float speed) {
    // Deliberately dirty the stack before constructing the mode, so an unreset
    // resampler picks up something obviously wrong rather than zeros.
    volatile float scratch[8192];
    for (int i = 0; i < 8192; ++i) scratch[i] = 1.0e30f;
    (void)scratch[0];

    auto mode = std::make_unique<pedal::PolyOctaveMode>();
    mode->Init();
    auto params = pedal::mod_fx::ParamSet::make_default();
    params.p1 = 1.0f;
    params.p2 = 0.0f;
    params.depth = 0.0f;
    params.tone = 0.5f;
    params.speed = speed;
    mode->Prepare(params);

    float peak = 0.0f;
    double energy = 0.0;
    for (int sample = 0; sample < 24000; ++sample) {
      if ((sample % 48) == 0) mode->Prepare(params);
      const float input =
          0.6f * std::sin(kTwoPi * 196.0f * static_cast<float>(sample) / 48000.0f);
      const auto out = mode->Process({input, input}, params);
      requireFinite(out.left, "poly octave output must stay finite");
      peak = std::max(peak, std::fabs(out.left));
      if (sample > 12000) energy += static_cast<double>(out.left) * out.left;
    }
    return std::pair<float, double>{peak, energy};
  };

  for (const float speed : {0.05f, 0.35f, 1.0f, 10.0f}) {
    const auto [peak, energy] = run(speed);
    require(peak < 4.0f,
            "poly octave must not spike at startup at speed " + std::to_string(speed));
    // The minimum Tracking setting used to produce exact silence.
    require(energy > 1.0e-6,
            "poly octave must produce output at speed " + std::to_string(speed));
  }
}

// Poly Octave generates sub-harmonic content, so the bright end of Tone — a
// high-pass reaching 3 kHz — used to remove nearly all of it. The last tenth of
// knob travel dropped 33 dB (-6.1 dB at 0.9 to -39.7 dB at 1.0), which reads as
// a broken effect rather than a tone control. Tone must stay monotonic and
// usable across its whole range.
void verifyPolyOctaveToneHasNoCliff()
{
  constexpr float kTwoPi = 6.28318530718f;
  const auto* descriptor = ardor::findDaisyFxDescriptor("mod", "poly_octave");
  require(descriptor != nullptr, "poly_octave descriptor exists");

  const auto levelAt = [&](float tone) {
    auto params = ardor::defaultDaisyFxParams(*descriptor);
    for (auto it = params.begin(); it != params.end(); ++it) {
      if (it.value().is_number()) it.value() = 1.0f;   // everything at max
    }
    params["tone"] = tone;
    ardor::DaisyFxProcessor processor;
    std::string error;
    require(processor.configure("mod", params, 48000.0f, error), error);

    double energy = 0.0;
    for (int sample = 0; sample < 96000; ++sample) {
      const float t = static_cast<float>(sample) / 48000.0f;
      const float input = 0.6f * std::sin(kTwoPi * 196.0f * t);
      const auto out = processor.process({input, input});
      if (sample > 48000) energy += static_cast<double>(out.left) * out.left;
    }
    return energy;
  };

  const double atCentre = levelAt(0.5f);
  const double atNearTop = levelAt(0.9f);
  const double atTop = levelAt(1.0f);
  require(atCentre > 0.0, "poly octave must produce output at centre tone");

  // No cliff in the last tenth of travel.
  const double cliffDb = 10.0 * std::log10(atNearTop / atTop);
  require(cliffDb < 12.0, "poly octave tone must not fall off a cliff at full travel");

  // Full travel must stay within a usable range of centre.
  const double travelDb = 10.0 * std::log10(atCentre / atTop);
  require(travelDb < 20.0, "poly octave tone range must stay musically usable");
}

// Magnitude of `frequency` in `samples`, via a Hann-windowed DFT bin.
double toneMagnitude(const std::vector<float>& samples, double frequency, double rate)
{
  double real = 0.0;
  double imaginary = 0.0;
  double weight = 0.0;
  const std::size_t count = samples.size();
  for (std::size_t i = 0; i < count; ++i) {
    const double window = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) /
                                               static_cast<double>(count - 1));
    const double phase = 2.0 * M_PI * frequency * static_cast<double>(i) / rate;
    real += samples[i] * window * std::cos(phase);
    imaginary -= samples[i] * window * std::sin(phase);
    weight += window;
  }
  return 2.0 * std::sqrt(real * real + imaginary * imaginary) / weight;
}

// The granular shifter restarts each grain by jumping the read pointer. If that
// jump is not a whole number of input periods the leftover phase biases the
// output pitch, by an amount that depends on the note being played — a fixed
// jump measured anywhere from -923 to +829 cents across the guitar range, which
// is useless for anything that has to play an interval. The restart position is
// chosen by waveform similarity against the grain it will overlap, which
// removes both that detune and the carrier cancellation a self-matched search
// leaves behind.
void verifyPitchShifterTuning()
{
  constexpr double kRate = 48000.0;
  static std::vector<float> buffer(16384);

  for (const float semitones : {12.0f, 7.0f, 5.0f, -7.0f, -12.0f, -24.0f}) {
    for (const double note : {82.4, 110.0, 146.8, 196.0, 246.9, 329.6}) {
      pedal::PitchShifter shifter;
      shifter.Init(buffer.data(), buffer.size(), 48000.0f, 1024);
      shifter.SetShift(semitones);

      std::vector<float> out;
      out.reserve(32768);
      for (int n = 0; n < 32768 + 48000; ++n) {
        const float input =
            static_cast<float>(0.5 * std::sin(2.0 * M_PI * note * n / kRate));
        const float sample = shifter.Process(input);
        if (n >= 48000) out.push_back(sample);
      }

      const double expected = note * std::pow(2.0, semitones / 12.0);
      double bestMagnitude = 0.0;
      double bestFrequency = expected;
      for (int cents = -80; cents <= 80; cents += 2) {
        const double frequency = expected * std::pow(2.0, cents / 1200.0);
        const double magnitude = toneMagnitude(out, frequency, kRate);
        if (magnitude > bestMagnitude) {
          bestMagnitude = magnitude;
          bestFrequency = frequency;
        }
      }

      const double detune = 1200.0 * std::log2(bestFrequency / expected);
      const std::string where = std::to_string(static_cast<int>(semitones)) +
                                " st at " + std::to_string(static_cast<int>(note)) + " Hz";
      require(std::fabs(detune) < 20.0,
              "pitch shifter must stay in tune: " + where);
      // The shifted voice must also carry the energy, not leave it in the
      // grain-rate sidebands either side of the carrier.
      require(bestMagnitude > 0.35,
              "pitch shifter must put its energy at the shifted pitch: " + where);
    }
  }
}

// The Whammy preset selector must be discrete, and every step must name a
// different preset. A count that drifts out of step with the interval table in
// whammy_mode.cpp would silently mislabel presets rather than fail to build.
void verifyWhammyPresetSelector()
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("mod", "whammy");
  require(descriptor != nullptr, "whammy descriptor exists");

  for (const auto& parameter : descriptor->params) {
    if (parameter.key != "p2") continue;
    const auto spec = ardor::daisyFxParamControlSpec(*descriptor, parameter);
    require(spec.choiceValues.size() == static_cast<std::size_t>(pedal::WhammyMode::PRESET_COUNT),
            "whammy preset selector must offer one step per preset");

    std::vector<std::string> labels;
    for (const float value : spec.choiceValues) {
      auto label = ardor::formatDaisyFxParamValue(*descriptor, parameter, value);
      require(label.find('%') == std::string::npos,
              "whammy preset must render as a name, not a percentage");
      labels.push_back(std::move(label));
    }
    std::sort(labels.begin(), labels.end());
    require(std::adjacent_find(labels.begin(), labels.end()) == labels.end(),
            "every whammy preset step must name a distinct preset");
    return;
  }
  require(false, "whammy exposes a p2 preset parameter");
}

void verifyQueuedDelayTransition()
{
  pedal::DelayTapTransition transition;
  transition.SetTarget(100.0f);
  transition.SetTarget(200.0f);
  for (int sample = 0; sample < 2400; ++sample) {
    if ((sample % 48) == 0) transition.SetTarget(300.0f + static_cast<float>(sample));
    require(transition.from() == 100.0f && transition.to() == 200.0f,
            "continuous automation must not restart active tap anchors");
    transition.Advance();
  }
  require(transition.active() && transition.from() == 200.0f && transition.to() > 200.0f,
          "the newest automated target must follow the completed transition");
}

void verifyDelayCorrections()
{
  const auto* dualDescriptor = ardor::findDaisyFxDescriptor("delay", "dual");
  require(dualDescriptor != nullptr, "dual delay descriptor exists");
  auto dualParams = ardor::defaultDaisyFxParams(*dualDescriptor);
  dualParams["time"] = 0.0f;
  dualParams["repeats"] = 0.0f;
  dualParams["mix"] = 1.0f;
  dualParams["grit"] = 1.0f;
  dualParams["mod_dep"] = 0.0f;
  ardor::DaisyFxProcessor dual;
  std::string error;
  require(dual.configure("delay", dualParams, 48000.0f, error), error);
  float leftPeak = 0.0f;
  for (int frame = 0; frame < 5000; ++frame) {
    const auto output = dual.process({0.0f, frame == 0 ? 1.0f : 0.0f});
    leftPeak = std::max(leftPeak, std::fabs(output.left));
  }
  require(leftPeak > 0.45f, "full ping-pong must preserve right-only source material");

  const auto* lofiDescriptor = ardor::findDaisyFxDescriptor("delay", "lofi");
  require(lofiDescriptor != nullptr, "lo-fi delay descriptor exists");
  auto lofiParams = ardor::defaultDaisyFxParams(*lofiDescriptor);
  lofiParams["time"] = 0.0f;
  lofiParams["repeats"] = 0.0f;
  lofiParams["mix"] = 1.0f;
  lofiParams["filter"] = 0.5f;
  lofiParams["grit"] = 0.0f;
  lofiParams["mod_dep"] = 0.0f;
  ardor::DaisyFxProcessor lofi;
  require(lofi.configure("delay", lofiParams, 48000.0f, error), error);
  float lofiPeak = 0.0f;
  for (int frame = 0; frame < 4000; ++frame) {
    const auto output = lofi.process({frame == 0 ? 1.0f : 0.0f,
                                     frame == 0 ? 1.0f : 0.0f});
    lofiPeak = std::max({lofiPeak, std::fabs(output.left), std::fabs(output.right)});
  }
  require(lofiPeak > 0.999f, "zero Crush must be a unity-gain degradation bypass");

  const auto* patternDescriptor = ardor::findDaisyFxDescriptor("delay", "pattern");
  require(patternDescriptor != nullptr, "pattern delay descriptor exists");
  auto patternParams = ardor::defaultDaisyFxParams(*patternDescriptor);
  patternParams["time"] = 1.0f;
  patternParams["repeats"] = 0.0f;
  patternParams["mix"] = 1.0f;
  patternParams["grit"] = 0.0f;
  patternParams["mod_dep"] = 0.0f;
  ardor::DaisyFxProcessor pattern;
  require(pattern.configure("delay", patternParams, 48000.0f, error), error);
  float finalTapPeak = 0.0f;
  for (int frame = 0; frame < 362000; ++frame) {
    const auto output = pattern.process({frame == 0 ? 1.0f : 0.0f,
                                        frame == 0 ? 1.0f : 0.0f});
    if (frame > 359900) {
      finalTapPeak = std::max({finalTapPeak, std::fabs(output.left), std::fabs(output.right)});
    }
  }
  require(finalTapPeak > 0.29f, "Pattern's full 2.5-second base range must retain its third tap");
}

void verifyDelayStartup()
{
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    if (descriptor.blockType != "delay") continue;

    ardor::DaisyFxProcessor processor;
    std::string error;
    auto params = ardor::defaultDaisyFxParams(descriptor);
    params["time"] = 1.0f;
    params["repeats"] = 0.0f;
    params["mix"] = 1.0f;
    require(processor.configure(descriptor.blockType, params, 48000.0f, error), error);

    float peak = 0.0f;
    for (int sample = 0; sample < 1024; ++sample) {
      const float input = sample == 0 ? 1.0f : 0.0f;
      const auto output = processor.process({input, input});
      requireFinite(output.left, descriptor.mode + " delay left must remain finite");
      requireFinite(output.right, descriptor.mode + " delay right must remain finite");
      peak = std::max(peak, std::max(std::fabs(output.left), std::fabs(output.right)));
    }
    require(peak < 0.000001f, descriptor.mode + " delay must not start at a two-sample tap");
  }
}

void verifyPhysicalReverbMappings()
{
  using pedal::ReverbModeId;
  using pedal::map_param;
  using pedal::reverb_fx::ParamId;
  using pedal::reverb_fx::get_param_range;

  const auto bloomTime = map_param(0.5f, get_param_range(ReverbModeId::Bloom, ParamId::Param1));
  const auto bloomFeedback = map_param(1.0f, get_param_range(ReverbModeId::Bloom, ParamId::Param2));
  const auto swellRise = map_param(1.0f, get_param_range(ReverbModeId::Swell, ParamId::Param1));
  const auto choraleVowel = map_param(1.0f, get_param_range(ReverbModeId::Chorale, ParamId::Param1));
  const auto magnetoHeads = map_param(0.0f, get_param_range(ReverbModeId::Magneto, ParamId::Param1));
  const auto springCount = map_param(0.5f, get_param_range(ReverbModeId::Spring, ParamId::Param2));

  require(std::fabs(bloomTime - 1.625f) < 0.000001f, "Bloom time map is physical seconds");
  require(std::fabs(bloomFeedback - 0.7f) < 0.000001f, "Bloom feedback map is physical gain");
  require(std::fabs(swellRise - 4.0f) < 0.000001f, "Swell rise map is physical seconds");
  require(std::fabs(choraleVowel - 6.0f) < 0.000001f, "Chorale vowel map is a physical index");
  require(std::fabs(magnetoHeads - 1.0f) < 0.000001f, "Magneto head map is a physical selector");
  require(std::fabs(springCount - 2.0f) < 0.000001f, "Spring count map is a physical selector");
}

void verifyEightLineFdn()
{
  std::array<std::array<float, 521>, pedal::Fdn::MAX_LINES> buffers{};
  pedal::Fdn::Config config{};
  config.n_lines = 8;
  config.sample_rate = 24000.0f;
  constexpr std::array<std::size_t, 8> sizes{257, 283, 313, 347, 379, 419, 461, 509};
  for (int line = 0; line < pedal::Fdn::MAX_LINES; ++line) {
    config.bufs[line] = buffers[line].data();
    config.delays[line] = sizes[static_cast<std::size_t>(line)];
  }

  pedal::Fdn fdn;
  fdn.Init(config);
  fdn.SetDecay(1.5f);
  fdn.SetDampFromRt60Ratio(1.5f, 0.7f);
  fdn.SetModulation(4.0f);

  float peak = 0.0f;
  double stereoDifference = 0.0;
  for (int sample = 0; sample < 12000; ++sample) {
    if ((sample % 48) == 0) fdn.PrepareBlock();
    const auto output = fdn.Process({sample == 0 ? 1.0f : 0.0f, 0.0f});
    requireFinite(output.left, "8-line FDN left output must remain finite");
    requireFinite(output.right, "8-line FDN right output must remain finite");
    peak = std::max(peak, std::max(std::fabs(output.left), std::fabs(output.right)));
    stereoDifference += std::fabs(static_cast<double>(output.left) - output.right);
  }
  require(peak > 0.0001f && peak < 2.0f, "8-line FDN must produce a bounded tail");
  require(stereoDifference > 0.01, "8-line FDN must produce a stereo field");
}

void verifyReverbReset()
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("reverb", "room");
  require(descriptor != nullptr, "room reverb descriptor exists");

  auto params = ardor::defaultDaisyFxParams(*descriptor);
  params["mix"] = 1.0f;
  params["mod"] = 1.0f;

  ardor::DaisyFxProcessor processor;
  std::string error;
  require(processor.configure("reverb", params, 48000.0f, error), error);

  const auto render = [&processor]() {
    std::array<ardor::StereoSample, 1024> output{};
    for (std::size_t i = 0; i < output.size(); ++i) {
      const float input = i == 0 ? 1.0f : 0.0f;
      output[i] = processor.process({input, input});
    }
    return output;
  };

  processor.reset();
  const auto first = render();
  processor.reset();
  const auto second = render();
  for (std::size_t i = 0; i < first.size(); ++i) {
    require(first[i].left == second[i].left && first[i].right == second[i].right,
            "reverb reset must restore deterministic FDN modulation state");
  }
}

void verifyReverbLatency()
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("reverb", "room");
  require(descriptor != nullptr, "room reverb descriptor exists");

  auto params = ardor::defaultDaisyFxParams(*descriptor);
  params["mix"] = 0.0f;
  ardor::DaisyFxProcessor processor;
  std::string error;
  require(processor.configure("reverb", params, 48000.0f, error), error);
  require(processor.latencyFrames() == 31U, "reverb boundary reports its fixed latency");

  for (std::size_t sample = 0; sample <= processor.latencyFrames(); ++sample) {
    const auto output = processor.process({sample == 0 ? 1.0f : 0.0f, 0.0f});
    const float expected = sample == processor.latencyFrames() ? 1.0f : 0.0f;
    require(output.left == expected && output.right == 0.0f,
            "dry reverb path must match the resampler latency exactly");
  }


  const auto* plateDescriptor = ardor::findDaisyFxDescriptor("reverb", "plate");
  require(plateDescriptor != nullptr, "plate reverb descriptor exists");
  auto plateParams = ardor::defaultDaisyFxParams(*plateDescriptor);
  plateParams["mix"] = 0.0f;
  require(processor.configure("reverb", plateParams, 48000.0f, error), error);
  require(processor.latencyFrames() == 0U, "native-rate plate must report zero adapter latency");
  const auto immediate = processor.process({1.0f, -0.5f});
  require(immediate.left == 1.0f && immediate.right == -0.5f,
          "native-rate plate dry path must not be delayed");
}

void verifyOutputMixSmoothing()
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("mod", "flanger");
  require(descriptor != nullptr, "flanger descriptor exists");
  auto dryParams = ardor::defaultDaisyFxParams(*descriptor);
  dryParams["mix"] = 0.0f;
  auto wetParams = dryParams;
  wetParams["mix"] = 1.0f;

  ardor::DaisyFxProcessor ramped;
  ardor::DaisyFxProcessor fullyWet;
  std::string error;
  require(ramped.configure("mod", dryParams, 48000.0f, error), error);
  require(fullyWet.configure("mod", wetParams, 48000.0f, error), error);

  for (int frame = 0; frame < 48; ++frame) {
    const float input = std::sin(static_cast<float>(frame) * 0.11f);
    (void)ramped.process({input, input});
    (void)fullyWet.process({input, input});
  }
  require(ramped.setParameterTarget("mix", 1.0f), "mix target must be accepted");

  const float input = std::sin(48.0f * 0.11f);
  const auto rampedOutput = ramped.process({input, input});
  const auto wetOutput = fullyWet.process({input, input});
  const float rampedDelta = std::fabs(rampedOutput.left - input);
  const float wetDelta = std::fabs(wetOutput.left - input);
  require(rampedDelta < wetDelta * 0.05f + 0.00001f,
          "mix automation must ramp rather than step to the wet signal");
}

} // namespace

int main()
{
  verifyToneFilter(24000.0f);
  verifyToneFilter(48000.0f);
  verifyHalfbandResamplers();
  verifyFastSineAccuracy();
  verifyBrightReverbs();
  verifyDelayLineReset();
  verifyHighQualityDelayTap();
  verifyTankRateInterpolation();
  verifyModulatedFdnRetainsHighFrequencies();
  verifyRotaryCrossoverHasNoNull();
  verifyPolyOctaveStartsClean();
  verifyPolyOctaveToneHasNoCliff();
  verifyPitchShifterTuning();
  verifyWhammyPresetSelector();
  verifyQueuedDelayTransition();
  verifyDelayCorrections();
  verifyDelayStartup();
  verifyPhysicalReverbMappings();
  verifyEightLineFdn();
  verifyReverbReset();
  verifyReverbLatency();
  verifyOutputMixSmoothing();
}
