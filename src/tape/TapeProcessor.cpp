#include "tape/TapeProcessor.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

constexpr float kTwoPi = 6.28318530718f;

// The field, in A/m, that a full-scale sample produces at the record head at
// unity drive. Chosen so a nominal signal sits in the knee of the default
// hysteresis fit rather than at either extreme.
constexpr float kFieldPerSample = 2.5e4f;

// The emphasis boost is capped rather than left as a true 6 dB/octave ramp,
// which would be unbounded. Twelve decibels is reached about two octaves above
// the corner, which is where a real machine's record amplifier gives up too.
constexpr float kEmphasisDb = 12.0f;

float clampedNumber(const nlohmann::json& params, const char* key, float fallback,
                    float low, float high)
{
  if (!params.is_object()) return fallback;
  const auto it = params.find(key);
  if (it == params.end() || !it->is_number()) return fallback;
  const float value = it->get<float>();
  return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

} // namespace

bool TapeProcessor::configure(const nlohmann::json& params, float sampleRate, std::string& error)
{
  error.clear();
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
    error = "tape sample rate must be finite and positive";
    return false;
  }
  const auto mode = params.value("mode", std::string{"tape"});
  if (mode != "tape") {
    error = "unsupported distortion mode: " + mode;
    return false;
  }
  const auto speed = params.value("speed", std::string{"15"});
  if (speed != "15" && speed != "30") {
    error = "tape speed must be 15 or 30, not " + speed;
    return false;
  }

  sampleRate_ = sampleRate;
  fastSpeed_ = speed == "30";

  driveDbTarget_ = clampedNumber(params, "drive", 0.0f, -12.0f, 24.0f);
  saturationTarget_ = clampedNumber(params, "saturation", 0.5f, 0.0f, 1.0f);
  biasTarget_ = clampedNumber(params, "bias", 0.5f, 0.0f, 1.0f);
  headBumpTarget_ = clampedNumber(params, "head_bump", 0.5f, 0.0f, 1.0f);
  mixTarget_ = clampedNumber(params, "mix", 1.0f, 0.0f, 1.0f);
  const float outputDb = clampedNumber(params, "output_db", 0.0f, -24.0f, 24.0f);
  outputTarget_ = std::pow(10.0f, outputDb / 20.0f);

  const float flutter = clampedNumber(params, "flutter", 0.0f, 0.0f, 1.0f);
  const float hissDb = clampedNumber(params, "hiss_db", TapeTransport::kHissOffDb,
                                     TapeTransport::kHissOffDb, -60.0f);

  // A knob turn has to reach the magnetics without stepping.
  constexpr float kSmoothingSeconds = 0.015f;
  smoothing_ = 1.0f - std::exp(-1.0f / (kSmoothingSeconds * sampleRate_));

  for (Lane* lane : {&left_, &right_}) {
    lane->dc.Init(sampleRate_);
  }
  transport_.configure(sampleRate_);
  transport_.setFlutter(flutter);
  transport_.setHissDb(hissDb);

  rebuildFilters();
  calibrateDriveMakeup();
  reset();
  return true;
}

TapeHysteresis::Parameters TapeProcessor::solverParameters() const
{
  // Saturation lowers the anhysteretic shape parameter, which brings the knee
  // in earlier and makes it harder. Bias moves the coercivity: under-bias
  // widens the loop and distorts more, over-bias narrows it and trades that
  // for high-frequency loss.
  auto parameters = TapeHysteresis::defaultParameters();
  parameters.anhystereticShape *= 1.6f - saturationTarget_;
  parameters.coercivity *= 1.6f - biasTarget_;
  return parameters;
}

void TapeProcessor::rebuildFilters()
{
  const float oversampledRate = sampleRate_ * static_cast<float>(kOversampling);

  // IEC emphasis: 35 us at 15 ips, 17.5 us at 30 ips.
  const float emphasisTau = fastSpeed_ ? 17.5e-6f : 35.0e-6f;
  const float emphasisHz = 1.0f / (kTwoPi * emphasisTau);

  // Head bump: the finite head core puts a low-frequency ripple in the
  // playback response. Bigger and lower at the slower speed.
  const float bumpHz = fastSpeed_ ? 90.0f : 45.0f;
  const float bumpDb = (fastSpeed_ ? 1.5f : 2.5f) * headBumpTarget_;

  // Over-bias costs high frequencies. Below the centre detent there is no loss.
  const float overBias = std::max(0.0f, biasTarget_ - 0.5f) * 2.0f;
  const float biasLossDb = -4.0f * overBias;

  const auto parameters = solverParameters();

  for (Lane* lane : {&left_, &right_}) {
    // Both halves of the emphasis pair run at the host rate. See processLane
    // for why they are not beside the solver at the oversampled rate.
    lane->preEmphasis.coefficients =
      makeHighShelf(sampleRate_, emphasisHz, 0.707f, kEmphasisDb);
    lane->deEmphasis.coefficients =
      makeHighShelf(sampleRate_, emphasisHz, 0.707f, -kEmphasisDb);
    lane->biasLoss.coefficients =
      makeHighShelf(sampleRate_, 6000.0f, 0.707f, biasLossDb);
    lane->bumpPeak.coefficients = makePeakingEq(sampleRate_, bumpHz, 1.2f, bumpDb);
    lane->bumpDip.coefficients =
      makePeakingEq(sampleRate_, bumpHz / 2.2f, 1.5f, -0.5f * bumpDb);
    lane->core.configure(parameters, oversampledRate);
  }
}

void TapeProcessor::calibrateDriveMakeup()
{
  // Drive must change character, not loudness. Rather than carry a hand-tuned
  // table that goes stale the moment the hysteresis fit moves, measure: run a
  // probe tone through a scratch copy of the magnetics at each calibration
  // point and store the reciprocal of what came out. This runs at load time,
  // never in the audio callback.
  const float oversampledRate = sampleRate_ * static_cast<float>(kOversampling);
  const auto parameters = solverParameters();

  constexpr float kProbeHz = 1000.0f;
  constexpr float kProbeAmplitude = 0.3f;
  const auto settle = static_cast<std::size_t>(oversampledRate / kProbeHz) * 4;
  const auto measure = static_cast<std::size_t>(oversampledRate / kProbeHz) * 16;

  // The target is the probe's own RMS, not the quietest measurement. Referring
  // to the quietest point would only flatten the drive curve and leave the
  // block roughly 30 dB down overall, because M/M_s at a nominal field is a
  // small number. Referring to the input makes the block unity-gain as well as
  // level-steady.
  const double target = kProbeAmplitude / std::sqrt(2.0);

  for (std::size_t point = 0; point < kCalibrationPoints; ++point) {
    const float driveDb = kCalibrationMinDb
      + (kCalibrationMaxDb - kCalibrationMinDb) * static_cast<float>(point)
        / static_cast<float>(kCalibrationPoints - 1);
    const float gain = std::pow(10.0f, driveDb / 20.0f);
    const float amplitude = gain * kProbeAmplitude * kFieldPerSample;

    TapeHysteresis probe;
    probe.configure(parameters, oversampledRate);
    probe.reset();

    for (std::size_t n = 0; n < settle; ++n) {
      const float t = static_cast<float>(n) / oversampledRate;
      probe.process(amplitude * std::sin(kTwoPi * kProbeHz * t));
    }
    double energy = 0.0;
    for (std::size_t n = 0; n < measure; ++n) {
      const float t = static_cast<float>(settle + n) / oversampledRate;
      const float out = probe.process(amplitude * std::sin(kTwoPi * kProbeHz * t));
      energy += static_cast<double>(out) * out;
    }
    const double level = std::sqrt(energy / static_cast<double>(measure));
    makeupTable_[point] = level > 1.0e-9 ? static_cast<float>(target / level) : 1.0f;
  }
}

float TapeProcessor::driveMakeup(float driveDb) const
{
  const float span = kCalibrationMaxDb - kCalibrationMinDb;
  const float position = (std::clamp(driveDb, kCalibrationMinDb, kCalibrationMaxDb)
                          - kCalibrationMinDb) / span * static_cast<float>(kCalibrationPoints - 1);
  const auto low = static_cast<std::size_t>(position);
  const std::size_t high = std::min(low + 1U, kCalibrationPoints - 1U);
  const float fraction = position - static_cast<float>(low);
  return makeupTable_[low] + fraction * (makeupTable_[high] - makeupTable_[low]);
}

bool TapeProcessor::setParameterTarget(const std::string& key, float value)
{
  if (!std::isfinite(value)) return false;
  if (key == "drive") {
    driveDbTarget_ = std::clamp(value, -12.0f, 24.0f);
    return true;
  }
  if (key == "output_db") {
    outputTarget_ = std::pow(10.0f, std::clamp(value, -24.0f, 24.0f) / 20.0f);
    return true;
  }
  if (key == "mix") {
    mixTarget_ = std::clamp(value, 0.0f, 1.0f);
    return true;
  }
  if (key == "flutter") {
    transport_.setFlutter(std::clamp(value, 0.0f, 1.0f));
    return true;
  }
  if (key == "hiss_db") {
    transport_.setHissDb(std::clamp(value, TapeTransport::kHissOffDb, -60.0f));
    return true;
  }
  // Saturation, bias and head bump change filter and solver coefficients, so
  // they rebuild rather than smooth. They are knob moves, not automation
  // targets. Saturation and bias also move the knee, so the drive makeup has
  // to be measured again or the level would drift as they turn.
  if (key == "saturation") {
    saturationTarget_ = std::clamp(value, 0.0f, 1.0f);
    rebuildFilters();
    calibrateDriveMakeup();
    return true;
  }
  if (key == "bias") {
    biasTarget_ = std::clamp(value, 0.0f, 1.0f);
    rebuildFilters();
    calibrateDriveMakeup();
    return true;
  }
  if (key == "head_bump") {
    headBumpTarget_ = std::clamp(value, 0.0f, 1.0f);
    rebuildFilters();
    return true;
  }
  // speed is a load-time choice: it is a `choice` control in the catalog and
  // reaches the engine by rebuilding the chain.
  return false;
}

void TapeProcessor::reset()
{
  for (Lane* lane : {&left_, &right_}) {
    lane->up2x.Reset();
    lane->up4x.Reset();
    lane->up8x.Reset();
    lane->down4x.Reset();
    lane->down2x.Reset();
    lane->down1x.Reset();
    lane->core.reset();
    lane->preEmphasis.clear();
    lane->deEmphasis.clear();
    lane->biasLoss.clear();
    lane->bumpPeak.clear();
    lane->bumpDip.clear();
    lane->trim.clear();
    lane->dc.Init(sampleRate_);
  }
  transport_.reset();
  dryLeft_.fill(0.0f);
  dryRight_.fill(0.0f);
  dryWrite_ = 0;

  driveDb_ = driveDbTarget_;
  mix_ = mixTarget_;
  output_ = outputTarget_;
  driveGain_ = std::pow(10.0f, driveDb_ / 20.0f);
  makeup_ = driveMakeup(driveDb_);
}

float TapeProcessor::processLane(Lane& lane, float input)
{
  // Pre-emphasis makes high frequencies reach the magnetics harder than low
  // ones. That asymmetry is the only reason the emphasis pair is worth having:
  // on a linear signal the pair simply cancels.
  //
  // It runs here, at the host rate and before the upsampler, rather than
  // beside the solver at 384 kHz. Run after upsampling it amplified the
  // halfband images — which sit around -60 dB — by up to four times, and that
  // mattered: the solver takes the sign of dH/dt from a one-sample difference,
  // and near the turning points of a low note the true dH/dt is small enough
  // for boosted image ripple to flip it. Every spurious flip switches the
  // hysteresis branch. Measured, that lifted 45 Hz by 4.7 dB and its third
  // harmonic by 12 dB while leaving 1 kHz untouched, because at 1 kHz the real
  // dH/dt is twenty times larger and swamps the ripple.
  //
  // Emphasising before the upsampler removes the mechanism at the root, and
  // costs one eighth as much arithmetic besides. Nothing is lost: the shelf's
  // shape above 24 kHz cannot matter to a signal that has nothing up there.
  const float emphasised = lane.preEmphasis.process(input);
  const float field = emphasised * driveGain_ * kFieldPerSample;

  const auto at2x = lane.up2x.Process(field);
  float at2xFiltered[2]{};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto at4x = lane.up4x.Process(at2x[i]);
    for (const float quarterRate : at4x) {
      const auto at8x = lane.up8x.Process(quarterRate);
      float at4xFiltered = 0.0f;
      for (const float sample : at8x) {
        const float magnetised = lane.core.process(sample);
        float decimated = 0.0f;
        if (lane.down4x.Push(magnetised, decimated)) at4xFiltered = decimated;
      }
      float decimated = 0.0f;
      if (lane.down2x.Push(at4xFiltered, decimated)) at2xFiltered[i] = decimated;
    }
  }
  float wet = 0.0f;
  for (const float sample : at2xFiltered) (void)lane.down1x.Push(sample, wet);

  wet = lane.deEmphasis.process(wet);
  wet = lane.biasLoss.process(wet);
  wet = lane.bumpDip.process(lane.bumpPeak.process(wet));
  wet = lane.trim.process(wet * makeup_);
  return lane.dc.Process(wet);
}

float TapeProcessor::readDry(const std::array<float, kDryBufferSize>& buffer) const
{
  // A plain integer read. The wet path was padded to a whole number of frames
  // precisely so this could be one — see kWetTrim.
  return buffer[(dryWrite_ + kDryBufferSize - 1U - kBlockLatency) & kDryBufferMask];
}

StereoSample TapeProcessor::process(StereoSample input)
{
  driveDb_ += smoothing_ * (driveDbTarget_ - driveDb_);
  mix_ += smoothing_ * (mixTarget_ - mix_);
  output_ += smoothing_ * (outputTarget_ - output_);
  driveGain_ = std::pow(10.0f, driveDb_ / 20.0f);
  makeup_ = driveMakeup(driveDb_);

  dryLeft_[dryWrite_] = input.left;
  dryRight_[dryWrite_] = input.right;
  dryWrite_ = (dryWrite_ + 1U) & kDryBufferMask;

  StereoSample wet{processLane(left_, input.left), processLane(right_, input.right)};
  wet = transport_.process(wet);

  const float dryLeft = readDry(dryLeft_);
  const float dryRight = readDry(dryRight_);

  return {
    output_ * (dryLeft + mix_ * (wet.left - dryLeft)),
    output_ * (dryRight + mix_ * (wet.right - dryRight)),
  };
}

} // namespace ardor
