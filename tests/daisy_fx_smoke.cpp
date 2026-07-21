#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/DaisyFxCatalog.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::vector<ardor::StereoSample> renderBlock(ardor::DaisyFxProcessor& processor, std::size_t frames)
{
  std::vector<ardor::StereoSample> out;
  out.reserve(frames);
  for (std::size_t i = 0; i < frames; ++i) {
    out.push_back(processor.process({0.5f, 0.5f}));
  }
  return out;
}

double controlResponseDifference(const std::string& blockType, const std::string& mode,
                                 const std::string& key, float low, float high)
{
  const auto* descriptor = ardor::findDaisyFxDescriptor(blockType, mode);
  require(descriptor != nullptr, "control test descriptor exists");
  auto lowParams = ardor::defaultDaisyFxParams(*descriptor);
  auto highParams = lowParams;
  lowParams[key] = low;
  highParams[key] = high;
  lowParams["mix"] = 1.0f;
  highParams["mix"] = 1.0f;
  if (blockType == "mod") {
    // The hosted modulation Level range is 0..2; 0.5 is unity.
    lowParams["level"] = 0.5f;
    highParams["level"] = 0.5f;
  }

  ardor::DaisyFxProcessor lowProcessor;
  ardor::DaisyFxProcessor highProcessor;
  std::string error;
  require(lowProcessor.configure(blockType, lowParams, 48000.0f, error), error);
  require(highProcessor.configure(blockType, highParams, 48000.0f, error), error);

  double difference = 0.0;
  for (int i = 0; i < 8192; ++i) {
    // Repeated note bursts exercise envelope-controlled effects as well as
    // steady-state modulation and early-reflection movement.
    const float envelope = (i % 1600) < 480 ? 1.0f : 0.0f;
    const float left = envelope * 0.38f * std::sin(6.28318530718f * 440.0f * static_cast<float>(i) / 48000.0f);
    const float right = envelope * 0.31f * std::sin(6.28318530718f * 659.0f * static_cast<float>(i) / 48000.0f);
    const auto a = lowProcessor.process({left, right});
    const auto b = highProcessor.process({left, right});
    difference += std::fabs(static_cast<double>(a.left) - b.left)
                + std::fabs(static_cast<double>(a.right) - b.right);
  }
  return difference;
}

} // namespace

int main()
{
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    ardor::DaisyFxProcessor catalogProcessor;
    std::string catalogError;
    require(catalogProcessor.configure(descriptor.blockType, ardor::defaultDaisyFxParams(descriptor),
                                       48000.0f, catalogError),
            catalogError);
    const auto sample = catalogProcessor.process({0.5f, 0.5f});
    require(std::isfinite(sample.left) && std::isfinite(sample.right), "catalog output finite");
  }

  // A bypass/re-enable cycle must not retain LFO, filter, or envelope state
  // from a previous phrase. Verify this for every delay implementation.
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    if (descriptor.kind != ardor::DaisyFxKind::Delay) {
      continue;
    }
    ardor::DaisyFxProcessor delayProcessor;
    std::string delayError;
    require(delayProcessor.configure(descriptor.blockType, ardor::defaultDaisyFxParams(descriptor),
                                     48000.0f, delayError),
            delayError);
    (void)renderBlock(delayProcessor, 192);
    delayProcessor.reset();
    const auto resetA = renderBlock(delayProcessor, 96);
    delayProcessor.reset();
    const auto resetB = renderBlock(delayProcessor, 96);
    for (std::size_t i = 0; i < resetA.size(); ++i) {
      require(resetA[i].left == resetB[i].left, "delay reset left deterministic");
      require(resetA[i].right == resetB[i].right, "delay reset right deterministic");
    }
  }

  ardor::DaisyFxProcessor processor;
  std::string error;
  const nlohmann::json params{
    {"mode", "vintage_trem"},
    {"speed", 0.7f},
    {"depth", 1.0f},
    {"mix", 1.0f},
    {"tone", 0.5f},
    {"p1", 0.0f},
    {"p2", 0.0f},
    {"level", 1.0f},
  };

  require(processor.configure("mod", params, 48000.0f, error), error);
  require(processor.tailFrames() == 0, "modulation has no offline tail estimate");
  require(!processor.configure("mod", params, 44100.0f, error), "non-48 kHz Daisy configuration should fail");
  require(error.find("48000") != std::string::npos, "non-48 kHz Daisy error should explain the constraint");
  require(processor.configure("mod", params, 48000.0f, error), error);
  const auto first = renderBlock(processor, 256);
  bool changed = false;
  for (const auto& sample : first) {
    require(std::isfinite(sample.left), "left output finite");
    require(std::isfinite(sample.right), "right output finite");
    require(std::fabs(sample.left) <= 1.0f, "left output bounded");
    require(std::fabs(sample.right) <= 1.0f, "right output bounded");
    changed = changed || std::fabs(sample.left - 0.5f) > 0.0001f;
  }
  require(changed, "vintage trem should change steady input");

  // Modulation state (notably LFO phase) is also per processor. Advancing one
  // instance must not alter a newly configured sibling's first output.
  ardor::DaisyFxProcessor modA;
  ardor::DaisyFxProcessor modB;
  ardor::DaisyFxProcessor modReference;
  require(modA.configure("mod", params, 48000.0f, error), error);
  require(modB.configure("mod", params, 48000.0f, error), error);
  require(modReference.configure("mod", params, 48000.0f, error), error);
  for (int i = 0; i < 137; ++i) {
    (void)modA.process({0.5f, 0.5f});
  }
  const auto isolatedMod = modB.process({0.5f, 0.5f});
  const auto referenceMod = modReference.process({0.5f, 0.5f});
  require(std::fabs(isolatedMod.left - referenceMod.left) < 0.000001f,
          "same-mode modulation instances must have independent phase state");
  require(std::fabs(isolatedMod.right - referenceMod.right) < 0.000001f,
          "same-mode modulation instances must have independent phase state");

  require(processor.setParameterTarget("depth", 0.0f), "publish live modulation target");
  require(!processor.setParameterTarget("mode", 0.0f), "mode is not a live target");
  const auto liveTargetOutput = renderBlock(processor, 48);
  require(std::isfinite(liveTargetOutput.back().left), "live target output finite");

  processor.reset();
  const auto afterResetA = renderBlock(processor, 64);
  processor.reset();
  const auto afterResetB = renderBlock(processor, 64);
  require(afterResetA.size() == afterResetB.size(), "reset block sizes");
  for (std::size_t i = 0; i < afterResetA.size(); ++i) {
    require(afterResetA[i].left == afterResetB[i].left, "reset left deterministic");
    require(afterResetA[i].right == afterResetB[i].right, "reset right deterministic");
  }

  nlohmann::json bad = params;
  bad["mode"] = "bogus";
  require(!processor.configure("mod", bad, 48000.0f, error), "unknown mode should fail");
  require(error.find("unsupported") != std::string::npos, "unknown mode error");

  nlohmann::json nonFinite = params;
  nonFinite["depth"] = std::numeric_limits<float>::infinity();
  require(!processor.configure("mod", nonFinite, 48000.0f, error), "non-finite Daisy parameter should fail");
  require(error.find("finite") != std::string::npos, "non-finite Daisy parameter error");

  const nlohmann::json delayParams{
    {"mode", "digital"},
    {"time", 0.02f},
    {"repeats", 0.35f},
    {"mix", 1.0f},
    {"filter", 0.5f},
    {"grit", 0.0f},
    {"mod_spd", 0.0f},
    {"mod_dep", 0.0f},
  };
  require(processor.configure("delay", delayParams, 48000.0f, error), error);
  const auto delayTail = processor.tailFrames();
  require(delayTail > 0 && delayTail < 60U * 48000U, "delay tail estimate is bounded");
  require(processor.setParameterTarget("time", 1.0f), "publish maximum delay time");
  require(processor.setParameterTarget("repeats", 1.0f), "publish maximum delay repeats");
  require(processor.tailFrames() == 60U * 48000U, "extreme delay tail estimate is capped");

  nlohmann::json patternTailParams = delayParams;
  patternTailParams["mode"] = "pattern";
  patternTailParams["grit"] = 0.0f;
  require(processor.configure("delay", patternTailParams, 48000.0f, error), error);
  const auto patternStraightTail = processor.tailFrames();
  patternTailParams["grit"] = 1.0f;
  require(processor.configure("delay", patternTailParams, 48000.0f, error), error);
  require(processor.tailFrames() < patternStraightTail,
          "triplet pattern tail must account for its shorter final tap");

  nlohmann::json swellDelayTailParams = delayParams;
  swellDelayTailParams["mode"] = "swell";
  swellDelayTailParams["mod_spd"] = 1.0f;
  swellDelayTailParams["mod_dep"] = 1.0f;
  require(processor.configure("delay", swellDelayTailParams, 48000.0f, error), error);
  const auto swellFastEnvelopeTail = processor.tailFrames();
  swellDelayTailParams["mod_spd"] = 0.0f;
  swellDelayTailParams["mod_dep"] = 0.0f;
  require(processor.configure("delay", swellDelayTailParams, 48000.0f, error), error);
  require(processor.tailFrames() > swellFastEnvelopeTail,
          "swell delay tail must include its attack and decay envelope");

  // Reconfigure the short-delay render case. The maximum-time live target above
  // must not produce a spurious two-sample echo before its real delay time.
  require(processor.configure("delay", delayParams, 48000.0f, error), error);
  bool delayChanged = false;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f});
    require(std::isfinite(sample.left), "delay left finite");
    require(std::isfinite(sample.right), "delay right finite");
    delayChanged = delayChanged || std::fabs(sample.left) > 0.0001f || std::fabs(sample.right) > 0.0001f;
  }
  require(delayChanged, "digital delay should produce wet output");

  // The dual-buffer clean delays must not collapse anti-phase stereo to mono
  // before writing their independent left/right delay histories.
  nlohmann::json antiPhaseDelayParams = delayParams;
  antiPhaseDelayParams["repeats"] = 0.0f;
  require(processor.configure("delay", antiPhaseDelayParams, 48000.0f, error), error);
  float antiPhaseLeftPeak = 0.0f;
  float antiPhaseRightTrough = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseLeftPeak = std::max(antiPhaseLeftPeak, sample.left);
    antiPhaseRightTrough = std::min(antiPhaseRightTrough, sample.right);
  }
  require(antiPhaseLeftPeak > 0.1f && antiPhaseRightTrough < -0.1f,
          "digital delay must preserve anti-phase stereo content");

  antiPhaseDelayParams["mode"] = "tape";
  require(processor.configure("delay", antiPhaseDelayParams, 48000.0f, error), error);
  antiPhaseLeftPeak = 0.0f;
  antiPhaseRightTrough = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseLeftPeak = std::max(antiPhaseLeftPeak, sample.left);
    antiPhaseRightTrough = std::min(antiPhaseRightTrough, sample.right);
  }
  require(antiPhaseLeftPeak > 0.1f && antiPhaseRightTrough < -0.1f,
          "tape delay must preserve anti-phase stereo content");

  antiPhaseDelayParams["mode"] = "dbucket";
  require(processor.configure("delay", antiPhaseDelayParams, 48000.0f, error), error);
  antiPhaseLeftPeak = 0.0f;
  antiPhaseRightTrough = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseLeftPeak = std::max(antiPhaseLeftPeak, sample.left);
    antiPhaseRightTrough = std::min(antiPhaseRightTrough, sample.right);
  }
  require(antiPhaseLeftPeak > 0.1f && antiPhaseRightTrough < -0.1f,
          "bucket brigade delay must preserve anti-phase stereo content");

  antiPhaseDelayParams["mode"] = "lofi";
  require(processor.configure("delay", antiPhaseDelayParams, 48000.0f, error), error);
  antiPhaseLeftPeak = 0.0f;
  antiPhaseRightTrough = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseLeftPeak = std::max(antiPhaseLeftPeak, sample.left);
    antiPhaseRightTrough = std::min(antiPhaseRightTrough, sample.right);
  }
  require(antiPhaseLeftPeak > 0.1f && antiPhaseRightTrough < -0.1f,
          "lo-fi delay must preserve anti-phase stereo content");

  antiPhaseDelayParams["mode"] = "duck";
  require(processor.configure("delay", antiPhaseDelayParams, 48000.0f, error), error);
  antiPhaseLeftPeak = 0.0f;
  antiPhaseRightTrough = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseLeftPeak = std::max(antiPhaseLeftPeak, sample.left);
    antiPhaseRightTrough = std::min(antiPhaseRightTrough, sample.right);
  }
  require(antiPhaseLeftPeak > 0.1f && antiPhaseRightTrough < -0.1f,
          "duck delay must preserve anti-phase stereo content");

  antiPhaseDelayParams["mode"] = "pattern";
  require(processor.configure("delay", antiPhaseDelayParams, 48000.0f, error), error);
  antiPhaseLeftPeak = 0.0f;
  antiPhaseRightTrough = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseLeftPeak = std::max(antiPhaseLeftPeak, sample.left);
    antiPhaseRightTrough = std::min(antiPhaseRightTrough, sample.right);
  }
  require(antiPhaseLeftPeak > 0.1f && antiPhaseRightTrough < -0.1f,
          "pattern delay must preserve anti-phase stereo content");

  antiPhaseDelayParams["mode"] = "swell";
  antiPhaseDelayParams["mod_spd"] = 1.0f;
  require(processor.configure("delay", antiPhaseDelayParams, 48000.0f, error), error);
  antiPhaseLeftPeak = 0.0f;
  antiPhaseRightTrough = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i < 256 ? 1.0f : 0.0f, i < 256 ? -1.0f : 0.0f});
    antiPhaseLeftPeak = std::max(antiPhaseLeftPeak, sample.left);
    antiPhaseRightTrough = std::min(antiPhaseRightTrough, sample.right);
  }
  require(antiPhaseLeftPeak > 0.1f && antiPhaseRightTrough < -0.1f,
          "swell delay must preserve anti-phase stereo content");

  antiPhaseDelayParams["mode"] = "trem";
  require(processor.configure("delay", antiPhaseDelayParams, 48000.0f, error), error);
  antiPhaseLeftPeak = 0.0f;
  antiPhaseRightTrough = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseLeftPeak = std::max(antiPhaseLeftPeak, sample.left);
    antiPhaseRightTrough = std::min(antiPhaseRightTrough, sample.right);
  }
  require(antiPhaseLeftPeak > 0.1f && antiPhaseRightTrough < -0.1f,
          "tremolo delay must preserve anti-phase stereo content");

  // Two effects of the same kind must not share delay storage. The silent
  // processor is stepped after the excited one so shared backing storage
  // would leak the first processor's repeats into its output.
  ardor::DaisyFxProcessor delayA;
  ardor::DaisyFxProcessor delayB;
  require(delayA.configure("delay", delayParams, 48000.0f, error), error);
  require(delayB.configure("delay", delayParams, 48000.0f, error), error);
  float delayATail = 0.0f;
  float delayBPeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto a = delayA.process({i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f});
    const auto b = delayB.process({0.0f, 0.0f});
    delayATail = std::max(delayATail, std::max(std::fabs(a.left), std::fabs(a.right)));
    delayBPeak = std::max(delayBPeak, std::max(std::fabs(b.left), std::fabs(b.right)));
  }
  require(delayATail > 0.0001f, "excited delay should produce a tail");
  require(delayBPeak < 0.000001f, "same-mode delay instances must be isolated");

  // Digital grit must color the feedback loop while leaving the first clean
  // repeat available at zero; it is no longer a catalog-only dead control.
  nlohmann::json cleanDigitalParams = delayParams;
  cleanDigitalParams["repeats"] = 0.8f;
  cleanDigitalParams["grit"] = 0.0f;
  nlohmann::json saturatedDigitalParams = cleanDigitalParams;
  saturatedDigitalParams["grit"] = 1.0f;
  ardor::DaisyFxProcessor cleanDigital;
  ardor::DaisyFxProcessor saturatedDigital;
  require(cleanDigital.configure("delay", cleanDigitalParams, 48000.0f, error), error);
  require(saturatedDigital.configure("delay", saturatedDigitalParams, 48000.0f, error), error);
  float colorationDifference = 0.0f;
  for (int i = 0; i < 16000; ++i) {
    const auto clean = cleanDigital.process({i == 0 ? 0.9f : 0.0f, i == 0 ? 0.9f : 0.0f});
    const auto saturated = saturatedDigital.process({i == 0 ? 0.9f : 0.0f, i == 0 ? 0.9f : 0.0f});
    colorationDifference += std::fabs(clean.left - saturated.left);
  }
  require(colorationDifference > 0.01f, "digital saturation must affect feedback repeats");

  // Bucket Brigade's Filter control must drive its output tone stage rather
  // than remain an exposed no-op.
  nlohmann::json darkBbdParams = delayParams;
  darkBbdParams["mode"] = "dbucket";
  darkBbdParams["filter"] = 0.0f;
  nlohmann::json brightBbdParams = darkBbdParams;
  brightBbdParams["filter"] = 1.0f;
  ardor::DaisyFxProcessor darkBbd;
  ardor::DaisyFxProcessor brightBbd;
  require(darkBbd.configure("delay", darkBbdParams, 48000.0f, error), error);
  require(brightBbd.configure("delay", brightBbdParams, 48000.0f, error), error);
  float toneDifference = 0.0f;
  for (int i = 0; i < 16000; ++i) {
    const float input = i == 0 ? 0.9f : 0.0f;
    const auto dark = darkBbd.process({input, input});
    const auto bright = brightBbd.process({input, input});
    toneDifference += std::fabs(dark.left - bright.left);
  }
  require(toneDifference > 0.01f, "bucket brigade filter must affect repeats");

  // Lo-fi Filter controls the anti-alias tone stage around the bit/sample-rate
  // reduction instead of remaining an exposed no-op.
  nlohmann::json darkLofiParams = delayParams;
  darkLofiParams["mode"] = "lofi";
  darkLofiParams["grit"] = 0.6f;
  darkLofiParams["filter"] = 0.0f;
  nlohmann::json brightLofiParams = darkLofiParams;
  brightLofiParams["filter"] = 1.0f;
  ardor::DaisyFxProcessor darkLofi;
  ardor::DaisyFxProcessor brightLofi;
  require(darkLofi.configure("delay", darkLofiParams, 48000.0f, error), error);
  require(brightLofi.configure("delay", brightLofiParams, 48000.0f, error), error);
  toneDifference = 0.0f;
  for (int i = 0; i < 16000; ++i) {
    const float input = i == 0 ? 0.9f : 0.0f;
    const auto dark = darkLofi.process({input, input});
    const auto bright = brightLofi.process({input, input});
    toneDifference += std::fabs(dark.left - bright.left);
  }
  require(toneDifference > 0.01f, "lo-fi filter must affect repeats");

  // Tremolo Delay's Shape control must alter the tremolo contour while
  // mod_dep remains responsible for the amount of gain modulation.
  nlohmann::json sineTremParams = delayParams;
  sineTremParams["mode"] = "trem";
  sineTremParams["grit"] = 0.0f;
  sineTremParams["mod_dep"] = 1.0f;
  sineTremParams["mod_spd"] = 0.5f;
  nlohmann::json shapedTremParams = sineTremParams;
  shapedTremParams["grit"] = 1.0f;
  ardor::DaisyFxProcessor sineTrem;
  ardor::DaisyFxProcessor shapedTrem;
  require(sineTrem.configure("delay", sineTremParams, 48000.0f, error), error);
  require(shapedTrem.configure("delay", shapedTremParams, 48000.0f, error), error);
  float shapeDifference = 0.0f;
  for (int i = 0; i < 16000; ++i) {
    const float input = 0.4f * std::sin(0.02f * static_cast<float>(i));
    const auto sine = sineTrem.process({input, input});
    const auto shaped = shapedTrem.process({input, input});
    shapeDifference += std::fabs(sine.left - shaped.left);
  }
  require(shapeDifference > 0.01f, "tremolo shape must affect repeats");

  // Swell's Threshold control must alter which note levels trigger an
  // envelope, with zero preserving the legacy 0.05 threshold.
  nlohmann::json lowThresholdSwell = delayParams;
  lowThresholdSwell["mode"] = "swell";
  lowThresholdSwell["time"] = 0.0f;
  lowThresholdSwell["mod_spd"] = 1.0f;
  lowThresholdSwell["mod_dep"] = 1.0f;
  lowThresholdSwell["grit"] = 0.0f;
  nlohmann::json highThresholdSwell = lowThresholdSwell;
  highThresholdSwell["grit"] = 1.0f;
  ardor::DaisyFxProcessor lowThreshold;
  ardor::DaisyFxProcessor highThreshold;
  require(lowThreshold.configure("delay", lowThresholdSwell, 48000.0f, error), error);
  require(highThreshold.configure("delay", highThresholdSwell, 48000.0f, error), error);
  float lowThresholdPeak = 0.0f;
  float highThresholdPeak = 0.0f;
  for (int i = 0; i < 8000; ++i) {
    const float input = i < 512 ? 0.12f : 0.0f;
    const auto low = lowThreshold.process({input, input});
    const auto high = highThreshold.process({input, input});
    lowThresholdPeak = std::max(lowThresholdPeak, std::fabs(low.left));
    highThresholdPeak = std::max(highThresholdPeak, std::fabs(high.left));
  }
  require(lowThresholdPeak > highThresholdPeak * 4.0f,
          "swell threshold must suppress quiet-note triggering at its maximum");

  // Swell Filter must shape the delayed swell rather than remain a no-op.
  nlohmann::json darkSwellParams = lowThresholdSwell;
  darkSwellParams["filter"] = 0.0f;
  nlohmann::json brightSwellParams = darkSwellParams;
  brightSwellParams["filter"] = 1.0f;
  ardor::DaisyFxProcessor darkSwell;
  ardor::DaisyFxProcessor brightSwell;
  require(darkSwell.configure("delay", darkSwellParams, 48000.0f, error), error);
  require(brightSwell.configure("delay", brightSwellParams, 48000.0f, error), error);
  toneDifference = 0.0f;
  for (int i = 0; i < 8000; ++i) {
    const float input = i < 512 ? 0.6f : 0.0f;
    const auto dark = darkSwell.process({input, input});
    const auto bright = brightSwell.process({input, input});
    toneDifference += std::fabs(dark.left - bright.left);
  }
  require(toneDifference > 0.01f, "swell filter must affect delayed output");

  const nlohmann::json reverbParams{
    {"mode", "room"},
    {"decay", 0.45f},
    {"pre_delay", 0.0f},
    {"mix", 1.0f},
    {"tone", 0.5f},
    {"mod", 0.0f},
    {"param1", 0.5f},
    {"param2", 0.5f},
  };
  require(processor.configure("reverb", reverbParams, 48000.0f, error), error);
  require(processor.tailFrames() > 0, "reverb has an offline tail estimate");

  nlohmann::json magnetoTailParams = reverbParams;
  magnetoTailParams["mode"] = "magneto";
  magnetoTailParams["pre_delay"] = 0.0f;
  require(processor.configure("reverb", magnetoTailParams, 48000.0f, error), error);
  const auto magnetoLowFeedbackTail = processor.tailFrames();
  magnetoTailParams["pre_delay"] = 1.0f;
  require(processor.configure("reverb", magnetoTailParams, 48000.0f, error), error);
  require(processor.tailFrames() > magnetoLowFeedbackTail,
          "Magneto tail estimate must include feedback repetitions");

  nlohmann::json swellTailParams = reverbParams;
  swellTailParams["mode"] = "swell";
  swellTailParams["param1"] = 0.0f;
  require(processor.configure("reverb", swellTailParams, 48000.0f, error), error);
  const auto swellShortRiseTail = processor.tailFrames();
  swellTailParams["param1"] = 1.0f;
  require(processor.configure("reverb", swellTailParams, 48000.0f, error), error);
  require(processor.tailFrames() > swellShortRiseTail,
          "Swell tail estimate must include rise time");

  nlohmann::json reflectionsTailParams = reverbParams;
  reflectionsTailParams["mode"] = "reflections";
  require(processor.configure("reverb", reflectionsTailParams, 48000.0f, error), error);
  require(processor.tailFrames() < 10000U,
          "Reflections tail estimate must use its fixed tap span, not generic decay");

  require(processor.configure("reverb", reverbParams, 48000.0f, error), error);
  bool reverbChanged = false;
  for (int i = 0; i < 2048; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f});
    require(std::isfinite(sample.left), "reverb left finite");
    require(std::isfinite(sample.right), "reverb right finite");
    reverbChanged = reverbChanged || std::fabs(sample.left) > 0.0001f || std::fabs(sample.right) > 0.0001f;
  }
  require(reverbChanged, "room reverb should produce wet output");

  // Room's stereo input path must retain side information. A mono fold-down
  // would cancel this anti-phase impulse before it reached the reverb core.
  require(processor.configure("reverb", reverbParams, 48000.0f, error), error);
  float antiPhaseReverbPeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseReverbPeak = std::max(antiPhaseReverbPeak,
                                   std::max(std::fabs(sample.left), std::fabs(sample.right)));
  }
  require(antiPhaseReverbPeak > 0.0001f, "room reverb must preserve anti-phase stereo content");

  nlohmann::json hallParams = reverbParams;
  hallParams["mode"] = "hall";
  require(processor.configure("reverb", hallParams, 48000.0f, error), error);
  float antiPhaseHallPeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseHallPeak = std::max(antiPhaseHallPeak,
                                 std::max(std::fabs(sample.left), std::fabs(sample.right)));
  }
  require(antiPhaseHallPeak > 0.0001f, "hall reverb must preserve anti-phase stereo content");

  nlohmann::json cloudParams = reverbParams;
  cloudParams["mode"] = "cloud";
  require(processor.configure("reverb", cloudParams, 48000.0f, error), error);
  float antiPhaseCloudPeak = 0.0f;
  for (int i = 0; i < 12000; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseCloudPeak = std::max(antiPhaseCloudPeak,
                                  std::max(std::fabs(sample.left), std::fabs(sample.right)));
  }
  require(antiPhaseCloudPeak > 0.0001f, "cloud reverb must preserve anti-phase stereo content");

  nlohmann::json bloomParams = reverbParams;
  bloomParams["mode"] = "bloom";
  require(processor.configure("reverb", bloomParams, 48000.0f, error), error);
  float antiPhaseBloomPeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseBloomPeak = std::max(antiPhaseBloomPeak,
                                  std::max(std::fabs(sample.left), std::fabs(sample.right)));
  }
  require(antiPhaseBloomPeak > 0.0001f, "bloom reverb must preserve anti-phase stereo content");

  nlohmann::json nonlinearParams = reverbParams;
  nonlinearParams["mode"] = "nonlinear";
  require(processor.configure("reverb", nonlinearParams, 48000.0f, error), error);
  float antiPhaseNonlinearPeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseNonlinearPeak = std::max(antiPhaseNonlinearPeak,
                                      std::max(std::fabs(sample.left), std::fabs(sample.right)));
  }
  require(antiPhaseNonlinearPeak > 0.0001f, "nonlinear reverb must preserve anti-phase stereo content");

  nlohmann::json swellParams = reverbParams;
  swellParams["mode"] = "swell";
  require(processor.configure("reverb", swellParams, 48000.0f, error), error);
  float antiPhaseSwellPeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseSwellPeak = std::max(antiPhaseSwellPeak,
                                  std::max(std::fabs(sample.left), std::fabs(sample.right)));
  }
  require(antiPhaseSwellPeak > 0.0001f, "swell reverb must preserve anti-phase stereo content");

  nlohmann::json choraleParams = reverbParams;
  choraleParams["mode"] = "chorale";
  require(processor.configure("reverb", choraleParams, 48000.0f, error), error);
  float antiPhaseChoralePeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseChoralePeak = std::max(antiPhaseChoralePeak,
                                    std::max(std::fabs(sample.left), std::fabs(sample.right)));
  }
  require(antiPhaseChoralePeak > 0.0001f, "chorale reverb must preserve anti-phase stereo content");

  nlohmann::json shimmerParams = reverbParams;
  shimmerParams["mode"] = "shimmer";
  require(processor.configure("reverb", shimmerParams, 48000.0f, error), error);
  float antiPhaseShimmerPeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseShimmerPeak = std::max(antiPhaseShimmerPeak,
                                    std::max(std::fabs(sample.left), std::fabs(sample.right)));
  }
  require(antiPhaseShimmerPeak > 0.0001f, "shimmer reverb must preserve anti-phase stereo content");

  nlohmann::json reflectionsParams = reverbParams;
  reflectionsParams["mode"] = "reflections";
  require(processor.configure("reverb", reflectionsParams, 48000.0f, error), error);
  float antiPhaseReflectionsPeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseReflectionsPeak = std::max(antiPhaseReflectionsPeak,
                                        std::max(std::fabs(sample.left), std::fabs(sample.right)));
  }
  require(antiPhaseReflectionsPeak > 0.0001f, "reflections reverb must preserve anti-phase stereo content");

  nlohmann::json magnetoParams = reverbParams;
  magnetoParams["mode"] = "magneto";
  require(processor.configure("reverb", magnetoParams, 48000.0f, error), error);
  float antiPhaseMagnetoPeak = 0.0f;
  for (int i = 0; i < 10000; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseMagnetoPeak = std::max(antiPhaseMagnetoPeak,
                                    std::max(std::fabs(sample.left), std::fabs(sample.right)));
  }
  require(antiPhaseMagnetoPeak > 0.0001f, "magneto reverb must preserve anti-phase stereo content");

  nlohmann::json springParams = reverbParams;
  springParams["mode"] = "spring";
  require(processor.configure("reverb", springParams, 48000.0f, error), error);
  float antiPhaseSpringPeak = 0.0f;
  for (int i = 0; i < 10000; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhaseSpringPeak = std::max(antiPhaseSpringPeak,
                                   std::max(std::fabs(sample.left), std::fabs(sample.right)));
  }
  require(antiPhaseSpringPeak > 0.0001f, "spring reverb must preserve anti-phase stereo content");

  nlohmann::json plateParams = reverbParams;
  plateParams["mode"] = "plate";
  require(processor.configure("reverb", plateParams, 48000.0f, error), error);
  float antiPhasePlatePeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? -1.0f : 0.0f});
    antiPhasePlatePeak = std::max(antiPhasePlatePeak,
                                  std::max(std::fabs(sample.left), std::fabs(sample.right)));
  }
  require(antiPhasePlatePeak > 0.0001f, "plate reverb must preserve anti-phase stereo content");

  ardor::DaisyFxProcessor roomA;
  ardor::DaisyFxProcessor roomB;
  require(roomA.configure("reverb", reverbParams, 48000.0f, error), error);
  require(roomB.configure("reverb", reverbParams, 48000.0f, error), error);
  float roomATail = 0.0f;
  float roomBPeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto a = roomA.process({i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f});
    const auto b = roomB.process({0.0f, 0.0f});
    roomATail = std::max(roomATail, std::max(std::fabs(a.left), std::fabs(a.right)));
    roomBPeak = std::max(roomBPeak, std::max(std::fabs(b.left), std::fabs(b.right)));
  }
  require(roomATail > 0.0001f, "excited room reverb should produce a tail");
  require(roomBPeak < 0.000001f, "same-mode reverb instances must be isolated");

  // Formant's Tone control is a post-bank dark/bright stage. Its centered
  // default remains transparent, but the two endpoints must not be a dead
  // catalog control.
  nlohmann::json formantParams = params;
  formantParams["mode"] = "formant";
  formantParams["mix"] = 1.0f;
  formantParams["level"] = 0.5f;
  auto renderFormantTone = [&](float tone) {
    formantParams["tone"] = tone;
    ardor::DaisyFxProcessor formant;
    require(formant.configure("mod", formantParams, 48000.0f, error), error);
    double energy = 0.0;
    for (int i = 0; i < 4096; ++i) {
      const float input = 0.4f * std::sin(6.28318530718f * 440.0f * static_cast<float>(i) / 48000.0f);
      const auto output = formant.process({input, input});
      energy += static_cast<double>(output.left) * output.left;
    }
    return energy;
  };
  const double formantDarkEnergy = renderFormantTone(0.0f);
  const double formantBrightEnergy = renderFormantTone(1.0f);
  require(std::fabs(formantDarkEnergy - formantBrightEnergy) > 1e-4,
          "formant Tone must change the post-formant response");

  // Every per-mode control made functional in this pass gets an endpoint
  // response assertion. Mix and Level are host-level controls and are tested
  // independently; these assertions cover only the former per-mode no-ops.
  require(controlResponseDifference("mod", "vintage_trem", "p1", 0.0f, 1.0f) > 1e-3,
          "vintage trem Shape must affect its contour");
  require(controlResponseDifference("mod", "vintage_trem", "tone", 0.0f, 1.0f) > 1e-3,
          "vintage trem Tone must affect its response");
  require(controlResponseDifference("mod", "vibe", "p2", 0.0f, 1.0f) > 1e-3,
          "vibe Shape must affect its photocell sweep");
  require(controlResponseDifference("mod", "poly_octave", "speed", 0.0f, 1.0f) > 1e-3,
          "poly octave Tracking must affect its response");
  require(controlResponseDifference("mod", "poly_octave", "tone", 0.0f, 1.0f) > 1e-3,
          "poly octave Tone must affect its response");
  require(controlResponseDifference("mod", "pattern_trem", "tone", 0.0f, 1.0f) > 1e-3,
          "pattern trem Tone must affect its response");
  require(controlResponseDifference("mod", "auto_swell", "tone", 0.0f, 1.0f) > 1e-3,
          "auto swell Tone must affect its response");
  require(controlResponseDifference("mod", "quadrature", "tone", 0.0f, 1.0f) > 1e-3,
          "quadrature Tone must affect its response");
  require(controlResponseDifference("reverb", "reflections", "mod", 0.0f, 1.0f) > 1e-3,
          "reflections Motion must affect its spatial response");
}
