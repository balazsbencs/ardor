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

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace ardor {

namespace {

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
  std::unique_ptr<pedal::ModMode> mod;
  pedal::mod_fx::ParamSet modParams = pedal::mod_fx::ParamSet::make_default();
  std::unique_ptr<pedal::DelayMode> delay;
  pedal::delay_fx::ParamSet delayParams = pedal::delay_fx::ParamSet::make_default();
  std::unique_ptr<pedal::ReverbMode> reverb;
  pedal::reverb_fx::ParamSet reverbParams = pedal::reverb_fx::ParamSet::make_default();

  void reset()
  {
    if (mod) mod->Reset();
    if (delay) delay->Reset();
    if (reverb) reverb->Reset();
  }
};

DaisyFxProcessor::DaisyFxProcessor() = default;
DaisyFxProcessor::~DaisyFxProcessor() = default;
DaisyFxProcessor::DaisyFxProcessor(DaisyFxProcessor&&) noexcept = default;
DaisyFxProcessor& DaisyFxProcessor::operator=(DaisyFxProcessor&&) noexcept = default;

bool DaisyFxProcessor::configure(const std::string& blockType, const nlohmann::json& params,
                                 float, std::string& error)
{
  error.clear();
  const std::string mode = params.value("mode", "");
  const auto* descriptor = findDaisyFxDescriptor(blockType, mode);
  if (!descriptor) {
    error = "unsupported Daisy effect: " + blockType + "/" + mode;
    return false;
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
    next->modParams.speed = mappedModParam(param("speed"), modeId, pedal::mod_fx::ParamId::Speed);
    next->modParams.depth = mappedModParam(param("depth"), modeId, pedal::mod_fx::ParamId::Depth);
    next->modParams.mix = mappedModParam(param("mix"), modeId, pedal::mod_fx::ParamId::Mix);
    next->modParams.tone = mappedModParam(param("tone"), modeId, pedal::mod_fx::ParamId::Tone);
    next->modParams.p1 = mappedModParam(param("p1"), modeId, pedal::mod_fx::ParamId::P1);
    next->modParams.p2 = mappedModParam(param("p2"), modeId, pedal::mod_fx::ParamId::P2);
    next->modParams.level = mappedModParam(param("level"), modeId, pedal::mod_fx::ParamId::Level);
    next->mod->Init();
    next->mod->Prepare(next->modParams);
  } else if (blockType == "delay") {
    pedal::DelayModeId modeId{};
    next->delay = makeDelayMode(mode, modeId);
    if (!next->delay) {
      error = "unsupported Daisy effect: " + blockType + "/" + mode;
      return false;
    }
    next->delayParams.time = mappedDelayParam(param("time"), modeId, pedal::delay_fx::ParamId::Time);
    next->delayParams.repeats = mappedDelayParam(param("repeats"), modeId, pedal::delay_fx::ParamId::Repeats);
    next->delayParams.mix = mappedDelayParam(param("mix"), modeId, pedal::delay_fx::ParamId::Mix);
    next->delayParams.filter = mappedDelayParam(param("filter"), modeId, pedal::delay_fx::ParamId::Filter);
    next->delayParams.grit = mappedDelayParam(param("grit"), modeId, pedal::delay_fx::ParamId::Grit);
    next->delayParams.mod_spd = mappedDelayParam(param("mod_spd"), modeId, pedal::delay_fx::ParamId::ModSpd);
    next->delayParams.mod_dep = mappedDelayParam(param("mod_dep"), modeId, pedal::delay_fx::ParamId::ModDep);
    next->delay->Init();
    next->delay->Prepare(next->delayParams);
  } else if (blockType == "reverb") {
    pedal::ReverbModeId modeId{};
    next->reverb = makeReverbMode(mode, modeId);
    if (!next->reverb) {
      error = "unsupported Daisy effect: " + blockType + "/" + mode;
      return false;
    }
    next->reverbParams.decay = mappedReverbParam(param("decay"), modeId, pedal::reverb_fx::ParamId::Decay);
    next->reverbParams.pre_delay = mappedReverbParam(param("pre_delay"), modeId, pedal::reverb_fx::ParamId::PreDelay);
    next->reverbParams.mix = mappedReverbParam(param("mix"), modeId, pedal::reverb_fx::ParamId::Mix);
    next->reverbParams.tone = mappedReverbParam(param("tone"), modeId, pedal::reverb_fx::ParamId::Tone);
    next->reverbParams.mod = mappedReverbParam(param("mod"), modeId, pedal::reverb_fx::ParamId::Mod);
    next->reverbParams.param1 = mappedReverbParam(param("param1"), modeId, pedal::reverb_fx::ParamId::Param1);
    next->reverbParams.param2 = mappedReverbParam(param("param2"), modeId, pedal::reverb_fx::ParamId::Param2);
    next->reverb->Init();
    next->reverb->Prepare(next->reverbParams);
  } else {
    error = "unsupported Daisy effect: " + blockType + "/" + mode;
    return false;
  }

  impl_ = std::move(next);
  return true;
}

void DaisyFxProcessor::reset()
{
  if (impl_) impl_->reset();
}

StereoSample DaisyFxProcessor::process(StereoSample input)
{
  if (!impl_) return input;

  if (impl_->mod) {
    const auto wet = impl_->mod->Process({input.left, input.right}, impl_->modParams);
    return {
      ((input.left * (1.0f - impl_->modParams.mix)) + (wet.left * impl_->modParams.mix)) * impl_->modParams.level,
      ((input.right * (1.0f - impl_->modParams.mix)) + (wet.right * impl_->modParams.mix)) * impl_->modParams.level,
    };
  }
  if (impl_->delay) {
    const auto wet = impl_->delay->Process((input.left + input.right) * 0.5f, impl_->delayParams);
    return {
      (input.left * (1.0f - impl_->delayParams.mix)) + (wet.left * impl_->delayParams.mix),
      (input.right * (1.0f - impl_->delayParams.mix)) + (wet.right * impl_->delayParams.mix),
    };
  }
  if (impl_->reverb) {
    const auto wet = impl_->reverb->Process((input.left + input.right) * 0.5f, impl_->reverbParams);
    return {
      (input.left * (1.0f - impl_->reverbParams.mix)) + (wet.left * impl_->reverbParams.mix),
      (input.right * (1.0f - impl_->reverbParams.mix)) + (wet.right * impl_->reverbParams.mix),
    };
  }
  return input;
}

} // namespace ardor
