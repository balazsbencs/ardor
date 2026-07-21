#include "daisyfx/DaisyFxProcessor.h"

#include "daisyfx/DaisyFxCatalog.h"

#include "audio/stereo_frame.h"
#include "config/delay_mode_id.h"
#include "config/mod_mode_id.h"
#include "config/reverb_mode_id.h"
#include "modes/auto_swell_mode.h"
#include "modes/bloom_reverb.h"
#include "modes/chorale_reverb.h"
#include "modes/chorus_mode.h"
#include "modes/cloud_reverb.h"
#include "modes/dbucket_delay.h"
#include "modes/delay_mode.h"
#include "modes/destroyer_mode.h"
#include "modes/digital_delay.h"
#include "modes/dual_delay.h"
#include "modes/duck_delay.h"
#include "modes/filter_delay.h"
#include "modes/filter_mode.h"
#include "modes/flanger_mode.h"
#include "modes/formant_mode.h"
#include "modes/hall_reverb.h"
#include "modes/lofi_delay.h"
#include "modes/magneto_reverb.h"
#include "modes/mod_mode.h"
#include "modes/nonlinear_reverb.h"
#include "modes/pattern_delay.h"
#include "modes/pattern_trem_mode.h"
#include "modes/phaser_mode.h"
#include "modes/plate_reverb.h"
#include "modes/poly_octave_mode.h"
#include "modes/quadrature_mode.h"
#include "modes/reflections_reverb.h"
#include "modes/reverb_mode.h"
#include "modes/room_reverb.h"
#include "modes/rotary_mode.h"
#include "modes/shimmer_reverb.h"
#include "modes/spring_reverb.h"
#include "modes/swell_delay.h"
#include "modes/swell_reverb.h"
#include "modes/tape_delay.h"
#include "modes/trem_delay.h"
#include "modes/vibe_mode.h"
#include "modes/vintage_trem_mode.h"
#include "params/delay_param_map.h"
#include "params/mod_param_map.h"
#include "params/reverb_param_map.h"
#include "dsp/halfband_resampler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>

namespace ardor {

namespace {

constexpr float kHostedDaisySampleRate = 48000.0f;
constexpr float kTailThreshold = 0.001f; // -60 dB
constexpr float kTailEstimateCapSeconds = 60.0f;
constexpr float kOutputSmoothingSeconds = 0.008f;
constexpr float kFeedbackSmoothingSeconds = 0.020f;

float finiteWet(float value) noexcept
{
  return std::isfinite(value) ? value : 0.0f;
}

size_t estimatedTailFrames(float seconds) noexcept
{
  if (!std::isfinite(seconds) || seconds <= 0.0f) {
    return 0;
  }
  const float cappedSeconds = std::min(seconds, kTailEstimateCapSeconds);
  return static_cast<size_t>(std::ceil(cappedSeconds * kHostedDaisySampleRate));
}

float normalizedParam(const nlohmann::json& params, const DaisyFxParamDescriptor& descriptor)
{
  const auto it = params.find(descriptor.key);
  if (it == params.end() || !it->is_number()) {
    return descriptor.defaultValue;
  }
  const float value = it->get<float>();
  if (!std::isfinite(value)) {
    return descriptor.defaultValue;
  }
  return std::clamp(value, 0.0f, 1.0f);
}

float mappedModParam(float normalized, pedal::ModModeId mode, pedal::mod_fx::ParamId param)
{
  return pedal::map_param(normalized, pedal::mod_fx::get_param_range(mode, param));
}

float mappedDelayParam(float normalized, pedal::DelayModeId mode, pedal::delay_fx::ParamId param)
{
  return pedal::map_param(normalized, pedal::delay_fx::get_param_range(mode, param));
}

float mappedReverbParam(float normalized, pedal::ReverbModeId mode, pedal::reverb_fx::ParamId param)
{
  return pedal::map_param(normalized, pedal::reverb_fx::get_param_range(mode, param));
}

std::unique_ptr<pedal::ModMode> makeModMode(const std::string& mode, pedal::ModModeId& id)
{
  if (mode == "chorus") { id = pedal::ModModeId::Chorus; return std::make_unique<pedal::ChorusMode>(); }
  if (mode == "flanger") { id = pedal::ModModeId::Flanger; return std::make_unique<pedal::FlangerMode>(); }
  if (mode == "rotary") { id = pedal::ModModeId::Rotary; return std::make_unique<pedal::RotaryMode>(); }
  if (mode == "vibe") { id = pedal::ModModeId::Vibe; return std::make_unique<pedal::VibeMode>(); }
  if (mode == "phaser") { id = pedal::ModModeId::Phaser; return std::make_unique<pedal::PhaserMode>(); }
  if (mode == "vintage_trem") { id = pedal::ModModeId::VintTrem; return std::make_unique<pedal::VintageTremMode>(); }
  if (mode == "poly_octave") { id = pedal::ModModeId::PolyOctave; return std::make_unique<pedal::PolyOctaveMode>(); }
  if (mode == "pattern_trem") { id = pedal::ModModeId::PatternTrem; return std::make_unique<pedal::PatternTremMode>(); }
  if (mode == "auto_swell") { id = pedal::ModModeId::AutoSwell; return std::make_unique<pedal::AutoSwellMode>(); }
  if (mode == "filter") { id = pedal::ModModeId::FilterFx; return std::make_unique<pedal::FilterMode>(); }
  if (mode == "formant") { id = pedal::ModModeId::FormantFx; return std::make_unique<pedal::FormantMode>(); }
  if (mode == "quadrature") { id = pedal::ModModeId::Quadrature; return std::make_unique<pedal::QuadratureMode>(); }
  if (mode == "destroyer") { id = pedal::ModModeId::Destroyer; return std::make_unique<pedal::DestroyerMode>(); }
  return {};
}

std::unique_ptr<pedal::DelayMode> makeDelayMode(const std::string& mode, pedal::DelayModeId& id)
{
  if (mode == "digital") { id = pedal::DelayModeId::Digital; return std::make_unique<pedal::DigitalDelay>(); }
  if (mode == "tape") { id = pedal::DelayModeId::Tape; return std::make_unique<pedal::TapeDelay>(); }
  if (mode == "dual") { id = pedal::DelayModeId::Dual; return std::make_unique<pedal::DualDelay>(); }
  if (mode == "filter") { id = pedal::DelayModeId::Filter; return std::make_unique<pedal::FilterDelay>(); }
  if (mode == "lofi") { id = pedal::DelayModeId::Lofi; return std::make_unique<pedal::LofiDelay>(); }
  if (mode == "dbucket") { id = pedal::DelayModeId::DBucket; return std::make_unique<pedal::DbucketDelay>(); }
  if (mode == "duck") { id = pedal::DelayModeId::Duck; return std::make_unique<pedal::DuckDelay>(); }
  if (mode == "pattern") { id = pedal::DelayModeId::Pattern; return std::make_unique<pedal::PatternDelay>(); }
  if (mode == "swell") { id = pedal::DelayModeId::Swell; return std::make_unique<pedal::SwellDelay>(); }
  if (mode == "trem") { id = pedal::DelayModeId::Trem; return std::make_unique<pedal::TremDelay>(); }
  return {};
}

std::unique_ptr<pedal::ReverbMode> makeReverbMode(const std::string& mode, pedal::ReverbModeId& id)
{
  if (mode == "room") { id = pedal::ReverbModeId::Room; return std::make_unique<pedal::RoomReverb>(); }
  if (mode == "hall") { id = pedal::ReverbModeId::Hall; return std::make_unique<pedal::HallReverb>(); }
  if (mode == "plate") { id = pedal::ReverbModeId::Plate; return std::make_unique<pedal::PlateReverb>(); }
  if (mode == "spring") { id = pedal::ReverbModeId::Spring; return std::make_unique<pedal::SpringReverb>(); }
  if (mode == "bloom") { id = pedal::ReverbModeId::Bloom; return std::make_unique<pedal::BloomReverb>(); }
  if (mode == "cloud") { id = pedal::ReverbModeId::Cloud; return std::make_unique<pedal::CloudReverb>(); }
  if (mode == "shimmer") { id = pedal::ReverbModeId::Shimmer; return std::make_unique<pedal::ShimmerReverb>(); }
  if (mode == "chorale") { id = pedal::ReverbModeId::Chorale; return std::make_unique<pedal::ChoraleReverb>(); }
  if (mode == "nonlinear") { id = pedal::ReverbModeId::Nonlinear; return std::make_unique<pedal::NonlinearReverb>(); }
  if (mode == "swell") { id = pedal::ReverbModeId::Swell; return std::make_unique<pedal::SwellReverb>(); }
  if (mode == "magneto") { id = pedal::ReverbModeId::Magneto; return std::make_unique<pedal::MagnetoReverb>(); }
  if (mode == "reflections") { id = pedal::ReverbModeId::Reflections; return std::make_unique<pedal::ReflectionsReverb>(); }
  return {};
}

} // namespace

struct DaisyFxProcessor::Impl {
  enum class Kind { None, Mod, Delay, Reverb };
  enum Target : size_t { Speed, Depth, Mix, Tone, P1, P2, Level, Count };
  Kind kind = Kind::None;
  pedal::ModModeId modId{};
  pedal::DelayModeId delayId{};
  pedal::ReverbModeId reverbId{};
  std::array<std::atomic<float>, Count> targets{};
  std::unique_ptr<pedal::ModMode> mod;
  pedal::mod_fx::ParamSet modParams = pedal::mod_fx::ParamSet::make_default();
  std::unique_ptr<pedal::DelayMode> delay;
  pedal::delay_fx::ParamSet delayParams = pedal::delay_fx::ParamSet::make_default();
  std::unique_ptr<pedal::ReverbMode> reverb;
  pedal::reverb_fx::ParamSet reverbParams = pedal::reverb_fx::ParamSet::make_default();
  pedal::HalfbandDecimator2x reverbDecimatorL;
  pedal::HalfbandDecimator2x reverbDecimatorR;
  pedal::HalfbandInterpolator2x reverbInterpolatorL;
  pedal::HalfbandInterpolator2x reverbInterpolatorR;
  std::array<pedal::StereoFrame, 2> reverbWetFrames{};
  size_t reverbWetFrameIndex = 0;
  size_t reverbWetFrameCount = 0;
  static constexpr size_t kReverbLatencyFrames = 31;
  std::array<StereoSample, kReverbLatencyFrames> reverbDryDelay{};
  size_t reverbDryDelayIndex = 0;
  size_t samplesUntilPrepare = 48;
  float smoothedMix = 0.0f;
  float smoothedLevel = 1.0f;
  bool controlSmoothingSeeded = false;

  size_t controlInterval() const
  {
    return kind == Kind::Reverb ? 96U : 48U;
  }

  void refreshParameters()
  {
    const auto target = [this](Target index) { return targets[index].load(std::memory_order_relaxed); };
    if (kind == Kind::Mod) {
      modParams.speed = mappedModParam(target(Speed), modId, pedal::mod_fx::ParamId::Speed);
      modParams.depth = mappedModParam(target(Depth), modId, pedal::mod_fx::ParamId::Depth);
      modParams.mix = mappedModParam(target(Mix), modId, pedal::mod_fx::ParamId::Mix);
      modParams.tone = mappedModParam(target(Tone), modId, pedal::mod_fx::ParamId::Tone);
      modParams.p1 = mappedModParam(target(P1), modId, pedal::mod_fx::ParamId::P1);
      modParams.p2 = mappedModParam(target(P2), modId, pedal::mod_fx::ParamId::P2);
      modParams.level = mappedModParam(target(Level), modId, pedal::mod_fx::ParamId::Level);
    } else if (kind == Kind::Delay) {
      delayParams.time = mappedDelayParam(target(Speed), delayId, pedal::delay_fx::ParamId::Time);
      const float targetRepeats = mappedDelayParam(target(Depth), delayId, pedal::delay_fx::ParamId::Repeats);
      if (!controlSmoothingSeeded) {
        delayParams.repeats = targetRepeats;
      } else {
        const float interval = static_cast<float>(controlInterval()) / kHostedDaisySampleRate;
        const float coefficient = 1.0f - std::exp(-interval / kFeedbackSmoothingSeconds);
        delayParams.repeats += coefficient * (targetRepeats - delayParams.repeats);
      }
      delayParams.mix = mappedDelayParam(target(Mix), delayId, pedal::delay_fx::ParamId::Mix);
      delayParams.filter = mappedDelayParam(target(Tone), delayId, pedal::delay_fx::ParamId::Filter);
      delayParams.grit = mappedDelayParam(target(P1), delayId, pedal::delay_fx::ParamId::Grit);
      delayParams.mod_spd = mappedDelayParam(target(P2), delayId, pedal::delay_fx::ParamId::ModSpd);
      delayParams.mod_dep = mappedDelayParam(target(Level), delayId, pedal::delay_fx::ParamId::ModDep);
    } else if (kind == Kind::Reverb) {
      const float targetDecay = mappedReverbParam(target(Speed), reverbId, pedal::reverb_fx::ParamId::Decay);
      if (!controlSmoothingSeeded) {
        reverbParams.decay = targetDecay;
      } else {
        const float interval = static_cast<float>(controlInterval()) / kHostedDaisySampleRate;
        const float coefficient = 1.0f - std::exp(-interval / kFeedbackSmoothingSeconds);
        reverbParams.decay += coefficient * (targetDecay - reverbParams.decay);
      }
      reverbParams.pre_delay = mappedReverbParam(target(Depth), reverbId, pedal::reverb_fx::ParamId::PreDelay);
      reverbParams.mix = mappedReverbParam(target(Mix), reverbId, pedal::reverb_fx::ParamId::Mix);
      reverbParams.tone = mappedReverbParam(target(Tone), reverbId, pedal::reverb_fx::ParamId::Tone);
      reverbParams.mod = mappedReverbParam(target(P1), reverbId, pedal::reverb_fx::ParamId::Mod);
      reverbParams.param1 = mappedReverbParam(target(P2), reverbId, pedal::reverb_fx::ParamId::Param1);
      reverbParams.param2 = mappedReverbParam(target(Level), reverbId, pedal::reverb_fx::ParamId::Param2);
    }
    controlSmoothingSeeded = true;
  }

  void prepare()
  {
    refreshParameters();
    if (mod) mod->Prepare(modParams);
    if (delay) delay->Prepare(delayParams);
    if (reverb) reverb->Prepare(reverbParams);
    samplesUntilPrepare = controlInterval();
  }

  void advanceControlRate()
  {
    if (samplesUntilPrepare == 0) {
      prepare();
    }
    --samplesUntilPrepare;
  }

  void seedOutputSmoothing()
  {
    if (kind == Kind::Mod) {
      smoothedMix = modParams.mix;
      smoothedLevel = modParams.level;
    } else if (kind == Kind::Delay) {
      smoothedMix = delayParams.mix;
      smoothedLevel = 1.0f;
    } else if (kind == Kind::Reverb) {
      smoothedMix = reverbParams.mix;
      smoothedLevel = 1.0f;
    }
  }

  void advanceOutputSmoothing()
  {
    const float targetMix = kind == Kind::Mod ? modParams.mix
                          : kind == Kind::Delay ? delayParams.mix : reverbParams.mix;
    const float step = 1.0f / (kOutputSmoothingSeconds * kHostedDaisySampleRate);
    smoothedMix += step * (targetMix - smoothedMix);
    if (kind == Kind::Mod) {
      smoothedLevel += step * (modParams.level - smoothedLevel);
    }
  }

  void reset()
  {
    if (mod) mod->Reset();
    if (delay) delay->Reset();
    if (reverb) reverb->Reset();
    reverbDecimatorL.Reset();
    reverbDecimatorR.Reset();
    reverbInterpolatorL.Reset();
    reverbInterpolatorR.Reset();
    reverbWetFrames = {};
    reverbWetFrameIndex = 0;
    reverbWetFrameCount = 0;
    reverbDryDelay = {};
    reverbDryDelayIndex = 0;
    controlSmoothingSeeded = false;
    seedOutputSmoothing();
    // Recompute control-rate coefficients before the first post-reset sample.
    samplesUntilPrepare = 0;
  }

  StereoSample delayReverbDry(StereoSample input)
  {
    const auto output = reverbDryDelay[reverbDryDelayIndex];
    reverbDryDelay[reverbDryDelayIndex] = input;
    reverbDryDelayIndex = (reverbDryDelayIndex + 1U) % kReverbLatencyFrames;
    return output;
  }

  pedal::StereoFrame processReverbWet(StereoSample input)
  {
    pedal::StereoFrame output{};
    if (reverbWetFrameCount != 0U) {
      output = reverbWetFrames[reverbWetFrameIndex++];
      --reverbWetFrameCount;
    }

    float decimatedLeft = 0.0f;
    float decimatedRight = 0.0f;
    const bool hasLeft = reverbDecimatorL.Push(input.left, decimatedLeft);
    const bool hasRight = reverbDecimatorR.Push(input.right, decimatedRight);
    if (hasLeft && hasRight) {
      const auto internal = reverb->Process({decimatedLeft, decimatedRight}, reverbParams);
      const auto left = reverbInterpolatorL.Process(finiteWet(internal.left));
      const auto right = reverbInterpolatorR.Process(finiteWet(internal.right));
      reverbWetFrames[0] = {left[0], right[0]};
      reverbWetFrames[1] = {left[1], right[1]};
      reverbWetFrameIndex = 0;
      reverbWetFrameCount = 2;
    }
    return output;
  }
};

DaisyFxProcessor::DaisyFxProcessor() = default;
DaisyFxProcessor::~DaisyFxProcessor() = default;
DaisyFxProcessor::DaisyFxProcessor(DaisyFxProcessor&&) noexcept = default;
DaisyFxProcessor& DaisyFxProcessor::operator=(DaisyFxProcessor&&) noexcept = default;

bool DaisyFxProcessor::configure(const std::string& blockType, const nlohmann::json& params,
                                 float sampleRate, std::string& error)
{
  error.clear();
  // The hosted vendor implementation has fixed 48 kHz delay lengths, LFO
  // increments, and reverb internals. Accepting another rate here would make
  // time-based effects audibly wrong even though configuration succeeded.
  if (!std::isfinite(sampleRate) || std::fabs(sampleRate - kHostedDaisySampleRate) > 0.5f) {
    error = "hosted Daisy effects require a 48000 Hz sample rate";
    return false;
  }

  const std::string mode = params.value("mode", "");
  const auto* descriptor = findDaisyFxDescriptor(blockType, mode);
  if (!descriptor) {
    error = "unsupported Daisy effect: " + blockType + "/" + mode;
    return false;
  }
  for (const auto& item : descriptor->params) {
    const auto it = params.find(item.key);
    if (it != params.end() && (!it->is_number() || !std::isfinite(it->get<float>()))) {
      error = "Daisy parameter must be finite: " + item.key;
      return false;
    }
  }

  auto next = std::make_unique<Impl>();
  auto param = [&](const char* key) -> float {
    for (const auto& item : descriptor->params) {
      if (item.key == key) return normalizedParam(params, item);
    }
    return 0.0f;
  };

  if (blockType == "mod") {
    pedal::ModModeId modeId{};
    next->mod = makeModMode(mode, modeId);
    if (!next->mod) {
      error = "unsupported Daisy effect: " + blockType + "/" + mode;
      return false;
    }
    next->kind = Impl::Kind::Mod;
    next->modId = modeId;
    next->targets[Impl::Speed].store(param("speed"));
    next->targets[Impl::Depth].store(param("depth"));
    next->targets[Impl::Mix].store(param("mix"));
    next->targets[Impl::Tone].store(param("tone"));
    next->targets[Impl::P1].store(param("p1"));
    next->targets[Impl::P2].store(param("p2"));
    next->targets[Impl::Level].store(param("level"));
    next->mod->Init();
    next->prepare();
    next->seedOutputSmoothing();
  } else if (blockType == "delay") {
    pedal::DelayModeId modeId{};
    next->delay = makeDelayMode(mode, modeId);
    if (!next->delay) {
      error = "unsupported Daisy effect: " + blockType + "/" + mode;
      return false;
    }
    next->kind = Impl::Kind::Delay;
    next->delayId = modeId;
    next->targets[Impl::Speed].store(param("time"));
    next->targets[Impl::Depth].store(param("repeats"));
    next->targets[Impl::Mix].store(param("mix"));
    next->targets[Impl::Tone].store(param("filter"));
    next->targets[Impl::P1].store(param("grit"));
    next->targets[Impl::P2].store(param("mod_spd"));
    next->targets[Impl::Level].store(param("mod_dep"));
    next->delay->Init();
    next->prepare();
    next->seedOutputSmoothing();
  } else if (blockType == "reverb") {
    pedal::ReverbModeId modeId{};
    next->reverb = makeReverbMode(mode, modeId);
    if (!next->reverb) {
      error = "unsupported Daisy effect: " + blockType + "/" + mode;
      return false;
    }
    next->kind = Impl::Kind::Reverb;
    next->reverbId = modeId;
    next->targets[Impl::Speed].store(param("decay"));
    next->targets[Impl::Depth].store(param("pre_delay"));
    next->targets[Impl::Mix].store(param("mix"));
    next->targets[Impl::Tone].store(param("tone"));
    next->targets[Impl::P1].store(param("mod"));
    next->targets[Impl::P2].store(param("param1"));
    next->targets[Impl::Level].store(param("param2"));
    next->reverb->Init();
    next->prepare();
    next->seedOutputSmoothing();
  } else {
    error = "unsupported Daisy effect: " + blockType + "/" + mode;
    return false;
  }

  impl_ = std::move(next);
  return true;
}

bool DaisyFxProcessor::setParameterTarget(const std::string& key, float normalized)
{
  if (!impl_ || !std::isfinite(normalized)) {
    return false;
  }
  std::optional<Impl::Target> target;
  if (key == "speed" || key == "time" || key == "decay") target = Impl::Speed;
  else if (key == "depth" || key == "repeats" || key == "pre_delay") target = Impl::Depth;
  else if (key == "mix") target = Impl::Mix;
  else if (key == "tone" || key == "filter") target = Impl::Tone;
  else if (key == "p1" || key == "grit" || key == "mod") target = Impl::P1;
  else if (key == "p2" || key == "mod_spd" || key == "param1") target = Impl::P2;
  else if (key == "level" || key == "mod_dep" || key == "param2") target = Impl::Level;
  if (!target.has_value()) {
    return false;
  }
  impl_->targets[*target].store(std::clamp(normalized, 0.0f, 1.0f), std::memory_order_release);
  return true;
}

void DaisyFxProcessor::reset()
{
  if (impl_) impl_->reset();
}

StereoSample DaisyFxProcessor::process(StereoSample input)
{
  if (!impl_) return input;
  // The vendor effects expect Prepare() at their 48-frame control interval.
  // Ardor's 64-frame audio quantum must not change those time constants.
  impl_->advanceControlRate();
  impl_->advanceOutputSmoothing();

  if (impl_->mod) {
    const auto wet = impl_->mod->Process({input.left, input.right}, impl_->modParams);
    return {
      ((input.left * (1.0f - impl_->smoothedMix)) + (finiteWet(wet.left) * impl_->smoothedMix)) * impl_->smoothedLevel,
      ((input.right * (1.0f - impl_->smoothedMix)) + (finiteWet(wet.right) * impl_->smoothedMix)) * impl_->smoothedLevel,
    };
  }
  if (impl_->delay) {
    const auto wet = impl_->delay->Process({input.left, input.right}, impl_->delayParams);
    return {
      (input.left * (1.0f - impl_->smoothedMix)) + (finiteWet(wet.left) * impl_->smoothedMix),
      (input.right * (1.0f - impl_->smoothedMix)) + (finiteWet(wet.right) * impl_->smoothedMix),
    };
  }
  if (impl_->reverb) {
    // Reverb algorithms are designed around a 24 kHz internal clock. The FIR
    // boundary restores that clock while retaining a 48 kHz host interface.
    const auto dry = impl_->delayReverbDry(input);
    const auto wet = impl_->processReverbWet(input);
    return {
      (dry.left * (1.0f - impl_->smoothedMix)) + (finiteWet(wet.left) * impl_->smoothedMix),
      (dry.right * (1.0f - impl_->smoothedMix)) + (finiteWet(wet.right) * impl_->smoothedMix),
    };
  }
  return input;
}

size_t DaisyFxProcessor::latencyFrames() const noexcept
{
  return impl_ && impl_->kind == Impl::Kind::Reverb ? Impl::kReverbLatencyFrames : 0;
}

size_t DaisyFxProcessor::tailFrames() const noexcept
{
  if (!impl_) {
    return 0;
  }

  const auto target = [this](Impl::Target index) {
    return impl_->targets[index].load(std::memory_order_relaxed);
  };

  if (impl_->kind == Impl::Kind::Delay) {
    const float mix = target(Impl::Mix);
    if (mix <= 0.0f) {
      return 0;
    }
    const float delaySeconds = mappedDelayParam(target(Impl::Speed), impl_->delayId,
                                                 pedal::delay_fx::ParamId::Time);
    const float repeats = mappedDelayParam(target(Impl::Depth), impl_->delayId,
                                           pedal::delay_fx::ParamId::Repeats);
    // A feedback delay remains audible until its repeated gain falls below
    // -60 dB. Account for the first repeat and a small stereo/modulation
    // margin; the public offline override handles deliberately extreme loops.
    float repeatsUntilSilent = 1.0f;
    if (repeats > 0.0f && repeats < 1.0f) {
      repeatsUntilSilent += std::ceil(std::log(kTailThreshold) / std::log(repeats));
    }
    float tailSeconds = delaySeconds * repeatsUntilSilent + 0.01f;
    switch (impl_->delayId) {
      case pedal::DelayModeId::Tape:
        // The right tape head is 150 host samples behind the primary head.
        tailSeconds += 150.0f / kHostedDaisySampleRate;
        break;
      case pedal::DelayModeId::Dual: {
        const float pingPong = mappedDelayParam(target(Impl::P1), impl_->delayId,
                                                  pedal::delay_fx::ParamId::Grit);
        // The secondary head reaches 1.5x the base period at full ping-pong.
        tailSeconds = delaySeconds * repeatsUntilSilent * (1.0f + 0.5f * pingPong) + 0.01f;
        break;
      }
      case pedal::DelayModeId::Pattern: {
        const float pattern = mappedDelayParam(target(Impl::P1), impl_->delayId,
                                                pedal::delay_fx::ParamId::Grit);
        const int patternIndex = std::clamp(static_cast<int>(pattern * 3.0f), 0, 2);
        constexpr std::array<float, 3> kLatestTapMultiplier = {3.0f, 3.0f, 2.0f};
        // Feedback is taken from the first tap, but the last repeat can still
        // emerge from the later rhythmic taps.
        tailSeconds = delaySeconds * (repeatsUntilSilent + kLatestTapMultiplier[patternIndex] - 1.0f) + 0.01f;
        break;
      }
      case pedal::DelayModeId::Swell: {
        const float modSpeed = mappedDelayParam(target(Impl::P2), impl_->delayId,
                                                 pedal::delay_fx::ParamId::ModSpd);
        const float modDepth = mappedDelayParam(target(Impl::Level), impl_->delayId,
                                                 pedal::delay_fx::ParamId::ModDep);
        const float speedNorm = std::clamp((modSpeed - 0.05f) / (10.0f - 0.05f), 0.0f, 1.0f);
        const float attackSeconds = 1.5f - 1.48f * speedNorm;
        const float decaySeconds = 2.5f - 2.42f * modDepth;
        tailSeconds += attackSeconds + decaySeconds;
        break;
      }
      default:
        break;
    }
    return estimatedTailFrames(tailSeconds);
  }

  if (impl_->kind == Impl::Kind::Reverb) {
    const float mix = target(Impl::Mix);
    if (mix <= 0.0f) {
      return 0;
    }
    const float decaySeconds = mappedReverbParam(target(Impl::Speed), impl_->reverbId,
                                                  pedal::reverb_fx::ParamId::Decay);
    const float preDelaySeconds = impl_->reverbId == pedal::ReverbModeId::Magneto
      ? 0.0f
      : mappedReverbParam(target(Impl::Depth), impl_->reverbId,
                          pedal::reverb_fx::ParamId::PreDelay);
    const float param1 = mappedReverbParam(target(Impl::P2), impl_->reverbId,
                                            pedal::reverb_fx::ParamId::Param1);
    const float param2 = mappedReverbParam(target(Impl::Level), impl_->reverbId,
                                            pedal::reverb_fx::ParamId::Param2);
    const float mod = mappedReverbParam(target(Impl::P1), impl_->reverbId,
                                         pedal::reverb_fx::ParamId::Mod);

    float tailSeconds = preDelaySeconds + decaySeconds + 0.1f;
    switch (impl_->reverbId) {
      case pedal::ReverbModeId::Magneto: {
        // Each tape-head period is repeated by the feedback loop until it is
        // below -60 dB. The implementation caps loop gain at 0.85.
        const float feedback = std::min(0.85f, mappedReverbParam(
            target(Impl::Depth), impl_->reverbId, pedal::reverb_fx::ParamId::PreDelay));
        float repeatsUntilSilent = 1.0f;
        if (feedback > 0.0f) {
          repeatsUntilSilent += std::ceil(std::log(kTailThreshold) / std::log(feedback));
        }
        tailSeconds = decaySeconds * repeatsUntilSilent + 0.02f;
        break;
      }
      case pedal::ReverbModeId::Reflections:
        // No feedback loop: maximum programmed reflection is 2,521 samples
        // at the 24 kHz core rate.
        tailSeconds = preDelaySeconds + (2521.0f / 24000.0f) + 0.01f;
        break;
      case pedal::ReverbModeId::Bloom:
        // Bloom's onset envelope can take up to param1 seconds to reveal a
        // tail; param2 adds a bounded output-feedback extension.
        tailSeconds = preDelaySeconds + decaySeconds + param1
                    + decaySeconds * (param2 / std::max(0.05f, 1.0f - param2));
        break;
      case pedal::ReverbModeId::Shimmer:
        // Pitch feedback is controlled by mod; reserve one additional RT60 at
        // full shimmer feedback without claiming an unbounded tail.
        tailSeconds = preDelaySeconds + decaySeconds * (1.0f + mod) + 0.15f;
        break;
      case pedal::ReverbModeId::Swell:
        tailSeconds = preDelaySeconds + param1 + decaySeconds + 0.1f;
        break;
      default:
        break;
    }
    return estimatedTailFrames(tailSeconds);
  }

  return 0;
}

} // namespace ardor
