#include "daisyfx/DaisyFxCatalog.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

const ardor::DaisyFxParamDescriptor& param(const ardor::DaisyFxDescriptor& effect,
                                           const std::string& key)
{
  for (const auto& candidate : effect.params) {
    if (candidate.key == key) return candidate;
  }
  throw std::runtime_error("missing parameter " + effect.mode + "/" + key);
}

} // namespace

int main()
{
  require(ardor::daisyFxCatalog().size() == 35, "all Daisy modes are cataloged");
  for (const auto& effect : ardor::daisyFxCatalog()) {
    require(!effect.blockType.empty() && !effect.mode.empty(), "effect has identifier");
    require(!effect.name.empty() && !effect.params.empty(), "effect has editable schema");
    require(ardor::findDaisyFxDescriptor(effect.blockType, effect.mode) == &effect,
            "catalog identifier resolves");
    for (const auto& parameter : effect.params) {
      for (const float value : {0.0f, parameter.defaultValue, 1.0f}) {
        const auto formatted = ardor::formatDaisyFxParamValue(effect, parameter, value);
        require(!formatted.empty() && formatted.find("nan") == std::string::npos,
                "every Daisy parameter has a finite display value");
      }
      const auto spec = ardor::daisyFxParamControlSpec(effect, parameter);
      if (spec.choiceValues.empty()) {
        require(spec.step <= 0.01f, "continuous Daisy controls are finer than the old five-percent step");
      } else {
        std::vector<std::string> labels;
        for (const float choiceValue : spec.choiceValues) {
          const auto label = ardor::formatDaisyFxParamValue(effect, parameter, choiceValue);
          require(std::find(labels.begin(), labels.end(), label) == labels.end(),
                  "every categorical snap position has a distinct label");
          labels.push_back(label);
        }
      }
    }
  }

  const auto* descriptor = ardor::findDaisyFxDescriptor("mod", "vintage_trem");
  require(descriptor != nullptr, "find vintage trem");
  require(descriptor->kind == ardor::DaisyFxKind::Mod, "vintage trem kind");
  require(descriptor->blockType == "mod", "vintage trem block type");
  require(descriptor->mode == "vintage_trem", "vintage trem mode");
  require(descriptor->name == "Vintage Trem", "vintage trem name");
  require(descriptor->params.size() == 7, "vintage trem param count");

  const auto defaults = ardor::defaultDaisyFxParams(*descriptor);
  require(defaults.value("mode", "") == "vintage_trem", "default mode");
  for (const auto* key : {"speed", "depth", "mix", "tone", "p1", "p2", "level"}) {
    require(defaults.contains(key), std::string{"default contains "} + key);
    require(defaults.at(key).is_number(), std::string{"default numeric "} + key);
  }

  require(ardor::findDaisyFxDescriptor("mod", "bogus") == nullptr, "reject unknown mode");
  require(ardor::findDaisyFxDescriptor("delay", "vintage_trem") == nullptr, "reject wrong block type");

  const auto* chorus = ardor::findDaisyFxDescriptor("mod", "chorus");
  require(chorus != nullptr, "find chorus");
  require(ardor::defaultDaisyFxParams(*chorus).value("mix", 0.0f) == 0.5f,
          "chorus defaults to equal dry/wet mix");

  for (const auto* mode : {"flanger", "phaser"}) {
    const auto* modulation = ardor::findDaisyFxDescriptor("mod", mode);
    require(modulation != nullptr, std::string{"find "} + mode);
    require(ardor::defaultDaisyFxParams(*modulation).value("mix", 0.0f) == 0.5f,
            std::string{mode} + " defaults to equal dry/wet mix");
  }

  const auto* filter = ardor::findDaisyFxDescriptor("mod", "filter");
  require(filter->params[4].label == "Resonance", "filter P1 names resonance");
  require(filter->params[5].label == "Shape / Source", "filter P2 names shape/source");

  const auto* formant = ardor::findDaisyFxDescriptor("mod", "formant");
  require(formant->params[4].label == "Resonance", "formant P1 names resonance");
  require(formant->params[5].label == "Vowel", "formant P2 names vowel");

  const auto* destroyer = ardor::findDaisyFxDescriptor("mod", "destroyer");
  require(destroyer->params[0].label == "Decimation", "destroyer speed names decimation");
  require(destroyer->params[1].label == "Bits", "destroyer depth names bits");
  require(destroyer->params[4].label == "Filter Resonance", "destroyer P1 names filter resonance");
  require(destroyer->params[5].label == "Noise", "destroyer P2 names noise");

  const auto* polyOctave = ardor::findDaisyFxDescriptor("mod", "poly_octave");
  require(polyOctave->params[1].label == "Oct Down 2", "poly octave depth names its -2 octave voice");

  const auto* autoSwell = ardor::findDaisyFxDescriptor("mod", "auto_swell");
  require(autoSwell->params[0].label == "Attack", "auto swell speed names attack");
  require(autoSwell->params[1].label == "Boost", "auto swell depth names boost");
  require(autoSwell->params[4].label == "Release", "auto swell P1 names release");
  require(autoSwell->params[5].label == "Doubling", "auto swell P2 names doubling");

  const auto* patternTrem = ardor::findDaisyFxDescriptor("mod", "pattern_trem");
  require(patternTrem->params[0].label == "Tempo", "pattern trem speed names tempo");
  require(patternTrem->params[5].label == "Division", "pattern trem P2 names rhythmic division");

  const auto* quadrature = ardor::findDaisyFxDescriptor("mod", "quadrature");
  require(quadrature->params[0].label == "Frequency", "quadrature speed names frequency");
  require(quadrature->params[1].label == "FM Depth", "quadrature depth names FM depth");
  require(quadrature->params[4].label == "Blend / Spread", "quadrature P1 names blend/spread");
  require(quadrature->params[5].label == "Mode", "quadrature P2 names mode");

  const auto* delay = ardor::findDaisyFxDescriptor("delay", "digital");
  require(delay != nullptr, "find digital delay");
  require(delay->kind == ardor::DaisyFxKind::Delay, "digital delay kind");
  require(ardor::defaultDaisyFxParams(*delay).value("mode", "") == "digital", "digital delay default mode");
  require(ardor::defaultDaisyFxParams(*delay).contains("repeats"), "digital delay defaults");
  require(delay->params[4].label == "Saturation", "digital delay names its saturation control");
  require(ardor::formatDaisyFxParamValue(*delay, param(*delay, "time"), 0.25f) == "98.1 ms",
          "delay time uses the hosted cubic range and milliseconds");
  require(ardor::formatDaisyFxParamValue(*delay, param(*delay, "repeats"), 1.0f) == "98%",
          "delay repeats shows the physical feedback ceiling");

  const auto* tape = ardor::findDaisyFxDescriptor("delay", "tape");
  require(ardor::formatDaisyFxParamValue(*tape, param(*tape, "time"), 1.0f) == "2500 ms",
          "tape delay maximum is shown in milliseconds");
  require(param(*tape, "mod_spd").label == "Flutter Rate", "tape modulation rate names flutter");
  require(ardor::daisyFxParamControlSpec(*tape, param(*tape, "time")).step == 0.001f,
          "delay time exposes fine normalized adjustment");

  const auto* filterDelay = ardor::findDaisyFxDescriptor("delay", "filter");
  require(filterDelay->params[4].label == "Filter Type", "filter delay names its filter selector");

  const auto* tremDelay = ardor::findDaisyFxDescriptor("delay", "trem");
  require(tremDelay->params[4].label == "Shape", "tremolo delay names its waveform control");
  require(param(*tremDelay, "mod_spd").label == "Trem Rate", "tremolo delay names its modulation rate");

  const auto* reverb = ardor::findDaisyFxDescriptor("reverb", "room");
  require(reverb != nullptr, "find room reverb");
  require(reverb->kind == ardor::DaisyFxKind::Reverb, "room reverb kind");
  require(ardor::defaultDaisyFxParams(*reverb).value("mode", "") == "room", "room reverb default mode");
  require(ardor::defaultDaisyFxParams(*reverb).contains("decay"), "room reverb defaults");
  require(ardor::formatDaisyFxParamValue(*reverb, param(*reverb, "decay"), 0.45f) == "2.00 s",
          "room decay is shown in seconds");

  const auto* shimmer = ardor::findDaisyFxDescriptor("reverb", "shimmer");
  require(shimmer != nullptr, "find shimmer reverb");
  require(shimmer->params[5].label == "Pitch 1", "shimmer first algorithm parameter is named");
  require(shimmer->params[6].label == "Pitch 2", "shimmer second algorithm parameter is named");
  const auto shimmerDefaults = ardor::defaultDaisyFxParams(*shimmer);
  require(shimmerDefaults.value("param1", 0.0f) == 24.0f / 36.0f, "shimmer defaults first voice to +12");
  require(shimmerDefaults.value("param2", 0.0f) == 19.0f / 36.0f, "shimmer defaults second voice to +7");
  require(ardor::formatDaisyFxParamValue(*shimmer, param(*shimmer, "param1"), 24.0f / 36.0f) == "+12 st",
          "shimmer pitch is shown in semitones");

  const auto* chorusType = ardor::findDaisyFxDescriptor("mod", "chorus");
  require(ardor::formatDaisyFxParamValue(*chorusType, param(*chorusType, "p2"), 0.65f) == "Detune",
          "modulation selectors show their actual mode names");
  require(ardor::daisyFxParamControlSpec(*chorusType, param(*chorusType, "p2")).choiceValues.size() == 5,
          "chorus type exposes exactly five snap positions");

  const auto* magneto = ardor::findDaisyFxDescriptor("reverb", "magneto");
  require(param(*magneto, "pre_delay").label == "Feedback", "magneto exposes feedback instead of pre-delay");
  require(ardor::formatDaisyFxParamValue(*magneto, param(*magneto, "param2"), 1.0f) == "Golden",
          "magneto spacing shows its discrete layout");
}
