#include "daisyfx/DaisyFxProcessor.h"

#include "daisyfx/DaisyFxCatalog.h"

#include "audio/stereo_frame.h"
#include "config/delay_mode_id.h"
#include "config/mod_mode_id.h"
#include "config/reverb_mode_id.h"
#include "modes/digital_delay.h"
#include "modes/room_reverb.h"
#include "modes/vintage_trem_mode.h"
#include "params/delay_param_map.h"
#include "params/mod_param_map.h"
#include "params/reverb_param_map.h"

#include <algorithm>
#include <cmath>
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

float mappedModParam(float normalized, pedal::mod_fx::ParamId param)
{
  return pedal::map_param(normalized, pedal::mod_fx::get_param_range(pedal::ModModeId::VintTrem, param));
}

float mappedDelayParam(float normalized, pedal::delay_fx::ParamId param)
{
  return pedal::map_param(normalized, pedal::delay_fx::get_param_range(pedal::DelayModeId::Digital, param));
}

float mappedReverbParam(float normalized, pedal::reverb_fx::ParamId param)
{
  return pedal::map_param(normalized, pedal::reverb_fx::get_param_range(pedal::ReverbModeId::Room, param));
}

} // namespace

struct DaisyFxProcessor::Impl {
  enum class Kind {
    Mod,
    Delay,
    Reverb
  };

  Kind kind = Kind::Mod;
  pedal::VintageTremMode vintageTrem;
  pedal::mod_fx::ParamSet params = pedal::mod_fx::ParamSet::make_default();
  pedal::DigitalDelay digitalDelay;
  pedal::delay_fx::ParamSet delayParams = pedal::delay_fx::ParamSet::make_default();
  pedal::RoomReverb roomReverb;
  pedal::reverb_fx::ParamSet reverbParams = pedal::reverb_fx::ParamSet::make_default();

  void reset()
  {
    vintageTrem.Reset();
    digitalDelay.Reset();
    roomReverb.Reset();
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
      if (item.key == key) {
        return normalizedParam(params, item);
      }
    }
    return 0.0f;
  };

  if (descriptor->blockType == "mod" && descriptor->mode == "vintage_trem") {
    next->kind = Impl::Kind::Mod;
    next->params.speed = mappedModParam(param("speed"), pedal::mod_fx::ParamId::Speed);
    next->params.depth = mappedModParam(param("depth"), pedal::mod_fx::ParamId::Depth);
    next->params.mix = mappedModParam(param("mix"), pedal::mod_fx::ParamId::Mix);
    next->params.tone = mappedModParam(param("tone"), pedal::mod_fx::ParamId::Tone);
    next->params.p1 = mappedModParam(param("p1"), pedal::mod_fx::ParamId::P1);
    next->params.p2 = mappedModParam(param("p2"), pedal::mod_fx::ParamId::P2);
    next->params.level = mappedModParam(param("level"), pedal::mod_fx::ParamId::Level);
    next->vintageTrem.Init();
    next->vintageTrem.Prepare(next->params);
  } else if (descriptor->blockType == "delay" && descriptor->mode == "digital") {
    next->kind = Impl::Kind::Delay;
    next->delayParams.time = mappedDelayParam(param("time"), pedal::delay_fx::ParamId::Time);
    next->delayParams.repeats = mappedDelayParam(param("repeats"), pedal::delay_fx::ParamId::Repeats);
    next->delayParams.mix = mappedDelayParam(param("mix"), pedal::delay_fx::ParamId::Mix);
    next->delayParams.filter = mappedDelayParam(param("filter"), pedal::delay_fx::ParamId::Filter);
    next->delayParams.grit = mappedDelayParam(param("grit"), pedal::delay_fx::ParamId::Grit);
    next->delayParams.mod_spd = mappedDelayParam(param("mod_spd"), pedal::delay_fx::ParamId::ModSpd);
    next->delayParams.mod_dep = mappedDelayParam(param("mod_dep"), pedal::delay_fx::ParamId::ModDep);
    next->digitalDelay.Init();
    next->digitalDelay.Prepare(next->delayParams);
  } else if (descriptor->blockType == "reverb" && descriptor->mode == "room") {
    next->kind = Impl::Kind::Reverb;
    next->reverbParams.decay = mappedReverbParam(param("decay"), pedal::reverb_fx::ParamId::Decay);
    next->reverbParams.pre_delay = mappedReverbParam(param("pre_delay"), pedal::reverb_fx::ParamId::PreDelay);
    next->reverbParams.mix = mappedReverbParam(param("mix"), pedal::reverb_fx::ParamId::Mix);
    next->reverbParams.tone = mappedReverbParam(param("tone"), pedal::reverb_fx::ParamId::Tone);
    next->reverbParams.mod = mappedReverbParam(param("mod"), pedal::reverb_fx::ParamId::Mod);
    next->reverbParams.param1 = mappedReverbParam(param("param1"), pedal::reverb_fx::ParamId::Param1);
    next->reverbParams.param2 = mappedReverbParam(param("param2"), pedal::reverb_fx::ParamId::Param2);
    next->roomReverb.Init();
    next->roomReverb.Prepare(next->reverbParams);
  } else {
    error = "unsupported Daisy effect: " + descriptor->blockType + "/" + descriptor->mode;
    return false;
  }
  impl_ = std::move(next);
  return true;
}

void DaisyFxProcessor::reset()
{
  if (impl_) {
    impl_->reset();
  }
}

StereoSample DaisyFxProcessor::process(StereoSample input)
{
  if (!impl_) {
    return input;
  }
  if (impl_->kind == Impl::Kind::Mod) {
    const auto wet = impl_->vintageTrem.Process({input.left, input.right}, impl_->params);
    return {
      ((input.left * (1.0f - impl_->params.mix)) + (wet.left * impl_->params.mix)) * impl_->params.level,
      ((input.right * (1.0f - impl_->params.mix)) + (wet.right * impl_->params.mix)) * impl_->params.level,
    };
  }
  if (impl_->kind == Impl::Kind::Delay) {
    const auto wet = impl_->digitalDelay.Process((input.left + input.right) * 0.5f, impl_->delayParams);
    return {
      (input.left * (1.0f - impl_->delayParams.mix)) + (wet.left * impl_->delayParams.mix),
      (input.right * (1.0f - impl_->delayParams.mix)) + (wet.right * impl_->delayParams.mix),
    };
  }

  const auto wet = impl_->roomReverb.Process((input.left + input.right) * 0.5f, impl_->reverbParams);
  return {
    (input.left * (1.0f - impl_->reverbParams.mix)) + (wet.left * impl_->reverbParams.mix),
    (input.right * (1.0f - impl_->reverbParams.mix)) + (wet.right * impl_->reverbParams.mix),
  };
}

} // namespace ardor
