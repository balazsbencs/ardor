#include "audio/WavIo.h"
#include "audio/EngineLoader.h"
#include "audio/MiniaudioBackend.h"
#include "dsp/IrConvolver.h"
#include "dsp/PedalEngine.h"
#include "preset/ChainPlan.h"
#include "NAM/model_config.h"
#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#if defined(__linux__)
#include <sched.h>
#endif

namespace {

constexpr double kPi = 3.14159265358979323846;

int require(bool condition)
{
  return condition ? 0 : 1;
}

// Independent, denser-than-the-loader check of max|H(f)|. The loader estimates
// the response peak on an 8x-oversampled FFT grid; this scans a much finer,
// grid-offset set of frequencies by direct DFT so a between-bin peak the loader
// missed would show up here.
double denseResponsePeak(const std::vector<float>& h, size_t grid)
{
  double peak = 0.0;
  for (size_t g = 0; g <= grid; ++g) {
    const double w = kPi * static_cast<double>(g) / static_cast<double>(grid);
    double re = 0.0;
    double im = 0.0;
    for (size_t n = 0; n < h.size(); ++n) {
      re += static_cast<double>(h[n]) * std::cos(w * static_cast<double>(n));
      im -= static_cast<double>(h[n]) * std::sin(w * static_cast<double>(n));
    }
    peak = std::max(peak, std::sqrt(re * re + im * im));
  }
  return peak;
}

} // namespace

int main()
{
  if (require(nam::ConfigParserRegistry::instance().has("SlimmableContainer"))) return 1;

  const auto wavPath = std::filesystem::temp_directory_path() / "ardor-wav-io-smoke.wav";
  {
    const float samples[] = {0.25f, -0.5f};
    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, 48000);
    ma_encoder encoder;
    if (require(ma_encoder_init_file(wavPath.string().c_str(), &cfg, &encoder) == MA_SUCCESS)) return 1;
    ma_encoder_write_pcm_frames(&encoder, samples, 2, nullptr);
    ma_encoder_uninit(&encoder);
  }
  const auto wav = ardor::readMonoWav(wavPath);
  if (require(wav.sampleRate == 48000)) return 1;
  if (require(wav.samples.size() == 2)) return 1;
  if (require(std::fabs(wav.samples[0] - 0.25f) < 0.0001f)) return 1;

  const auto wav441Path = std::filesystem::temp_directory_path() / "ardor-wav-io-44100-smoke.wav";
  {
    const float samples[] = {0.25f, -0.5f};
    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, 44100);
    ma_encoder encoder;
    if (require(ma_encoder_init_file(wav441Path.string().c_str(), &cfg, &encoder) == MA_SUCCESS)) return 1;
    ma_encoder_write_pcm_frames(&encoder, samples, 2, nullptr);
    ma_encoder_uninit(&encoder);
  }
  const auto wav441 = ardor::readMonoWav(wav441Path);
  if (require(wav441.sampleRate == 44100)) return 1;

  ardor::MonoWav emptyIr;
  emptyIr.sampleRate = 48000;
  emptyIr.channels = 1;
  std::string irValidationError;
  if (require(!ardor::prepareMonoIr(emptyIr, 8192, irValidationError))) return 1;
  if (require(irValidationError.find("no audio") != std::string::npos)) return 1;

  // The loader attenuates any IR whose response peak exceeds a -1 dB ceiling
  // using a small guard for between-bin estimation error. IRs below the guarded
  // threshold are left untouched.
  constexpr float kResponseCeiling = 0.8912509f; // -1 dB transfer gain.
  constexpr float kGuardMargin = 0.977f;
  constexpr float kGuardedCeiling = kResponseCeiling * kGuardMargin;

  // A capped IR whose retained response is below the ceiling is left unchanged
  // (aside from the truncation fade): {1,0.5,0.25,0.125} capped to 2 -> {0.5,0}.
  ardor::MonoWav cappedIr{{1.0f, 0.5f, 0.25f, 0.125f}, 48000, 1};
  if (require(ardor::prepareMonoIr(cappedIr, 2, irValidationError))) return 1;
  if (require(cappedIr.samples.size() == 2)) return 1;
  if (require(std::fabs(cappedIr.samples.front() - 0.5f) < 0.0001f)) return 1;
  if (require(cappedIr.samples.back() == 0.0f)) return 1;

  // A quiet IR (response peak 0.375 < ceiling) is NOT boosted - attenuation only.
  ardor::MonoWav quietIr{{0.25f, -0.125f}, 48000, 1};
  if (require(ardor::prepareMonoIr(quietIr, 0, irValidationError))) return 1;
  if (require(std::fabs(quietIr.samples.front() - 0.25f) < 0.0001f)) return 1;

  // Blast-safety regression: a near-silent / noise-only IR must be left exactly
  // as-is, never scaled up toward full level. (The interim equalizing version
  // divided by the response peak and turned this into a full-scale blast.)
  ardor::MonoWav nearSilentIr{{1.0e-7f, -5.0e-8f}, 48000, 1};
  if (require(ardor::prepareMonoIr(nearSilentIr, 0, irValidationError))) return 1;
  if (require(nearSilentIr.samples.front() == 1.0e-7f)) return 1;
  if (require(nearSilentIr.samples.back() == -5.0e-8f)) return 1;

  // An all-zero IR is valid and must stay all-zero (no divide-by-zero, no NaN).
  ardor::MonoWav zeroIr{{0.0f, 0.0f, 0.0f, 0.0f}, 48000, 1};
  if (require(ardor::prepareMonoIr(zeroIr, 0, irValidationError))) return 1;
  if (require(zeroIr.samples.front() == 0.0f && zeroIr.samples.back() == 0.0f)) return 1;

  // Regression for the NAM+IR clipping bug: a hot IR must be attenuated so its
  // response peak lands at the ceiling. All-ones peaks at DC (= sum of taps), an
  // exact FFT bin, so its response peak == sum. Peak-sample normalization left
  // this far above the ceiling, which stacked on the NAM output and clipped.
  ardor::MonoWav hotIr;
  hotIr.sampleRate = 48000;
  hotIr.channels = 1;
  hotIr.samples.assign(64, 1.0f);
  if (require(ardor::prepareMonoIr(hotIr, 0, irValidationError))) return 1;
  float hotDcGain = 0.0f;
  for (float sample : hotIr.samples) hotDcGain += sample;
  if (require(hotDcGain <= kResponseCeiling)) return 1;
  if (require(std::fabs(hotDcGain - kGuardedCeiling) < 0.01f)) return 1;

  // Off-bin robustness: a narrow resonator whose peak falls BETWEEN the loader's
  // FFT bins must still end at or below the ceiling when checked on an
  // independent, much denser grid. Its raw peak (~1/(1-r)) is far above the
  // ceiling, so it must be attenuated.
  ardor::MonoWav resonatorIr;
  resonatorIr.sampleRate = 48000;
  resonatorIr.channels = 1;
  resonatorIr.samples.resize(512);
  {
    const double r = 0.985;
    const double w0 = 1.0; // rad/sample, deliberately off the bin grid
    double envelope = 1.0;
    for (size_t n = 0; n < resonatorIr.samples.size(); ++n) {
      resonatorIr.samples[n] = static_cast<float>(envelope * std::cos(w0 * static_cast<double>(n)));
      envelope *= r;
    }
  }
  if (require(ardor::prepareMonoIr(resonatorIr, 0, irValidationError))) return 1;
  const double resonatorPeak = denseResponsePeak(resonatorIr.samples, 16000);
  if (require(resonatorPeak <= static_cast<double>(kResponseCeiling))) return 1;
  if (require(resonatorPeak > static_cast<double>(kResponseCeiling) * 0.85)) return 1;

  // Guard-threshold regression: put a finite cosine halfway between the loader's
  // FFT bins, then scale its sampled-bin peak below the nominal ceiling but above
  // the guarded ceiling. Its independently measured between-bin peak is above the
  // nominal ceiling, so the loader must attenuate it even though its FFT estimate
  // alone appears to fit below -1 dB.
  ardor::MonoWav boundaryIr;
  boundaryIr.sampleRate = 48000;
  boundaryIr.channels = 1;
  boundaryIr.samples.resize(512);
  constexpr size_t kLoaderFftSize = 4096; // nextPowerOfTwo(512 * 8)
  const double boundaryW = 2.0 * kPi * 650.5 / static_cast<double>(kLoaderFftSize);
  for (size_t n = 0; n < boundaryIr.samples.size(); ++n) {
    boundaryIr.samples[n] = static_cast<float>(std::cos(boundaryW * static_cast<double>(n)));
  }
  constexpr double kBoundaryGridTarget = 0.888;
  const double unscaledGridPeak = denseResponsePeak(boundaryIr.samples, kLoaderFftSize / 2);
  const float boundaryScale = static_cast<float>(kBoundaryGridTarget / unscaledGridPeak);
  for (float& sample : boundaryIr.samples) sample *= boundaryScale;

  const double boundaryGridPeak = denseResponsePeak(boundaryIr.samples, kLoaderFftSize / 2);
  const double boundaryDensePeakBefore = denseResponsePeak(boundaryIr.samples, 16000);
  if (require(boundaryGridPeak > static_cast<double>(kGuardedCeiling))) return 1;
  if (require(boundaryGridPeak < static_cast<double>(kResponseCeiling))) return 1;
  if (require(boundaryDensePeakBefore > static_cast<double>(kResponseCeiling))) return 1;

  const float boundaryFirstBefore = boundaryIr.samples.front();
  if (require(ardor::prepareMonoIr(boundaryIr, 0, irValidationError))) return 1;
  const double boundaryDensePeakAfter = denseResponsePeak(boundaryIr.samples, 16000);
  if (require(std::fabs(boundaryIr.samples.front()) < std::fabs(boundaryFirstBefore))) return 1;
  if (require(boundaryDensePeakAfter <= static_cast<double>(kResponseCeiling))) return 1;

  const auto stereoWavPath = std::filesystem::temp_directory_path() / "ardor-stereo-ir-smoke.wav";
  {
    const float samples[] = {0.25f, -0.25f};
    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, 48000);
    ma_encoder encoder;
    if (require(ma_encoder_init_file(stereoWavPath.string().c_str(), &cfg, &encoder) == MA_SUCCESS)) return 1;
    ma_encoder_write_pcm_frames(&encoder, samples, 1, nullptr);
    ma_encoder_uninit(&encoder);
  }
  bool stereoRejected = false;
  try {
    (void)ardor::readMonoWav(stereoWavPath);
  } catch (const std::exception&) {
    stereoRejected = true;
  }
  if (require(stereoRejected)) return 1;
  std::filesystem::remove(stereoWavPath);

  if (require(ardor::captureChannelCountForInput(0) == 2)) return 1;
  if (require(ardor::captureChannelCountForInput(1) == 2)) return 1;

  ardor::RealtimeStats schedulerStats;
  if (require(!ardor::hasRequiredRealtimeScheduler(schedulerStats))) return 1;
#if defined(__linux__)
  schedulerStats.schedulerCaptured = true;
  schedulerStats.schedulerPolicy = SCHED_FIFO;
  schedulerStats.schedulerPriority = 70;
  if (require(ardor::hasRequiredRealtimeScheduler(schedulerStats))) return 1;
  schedulerStats.schedulerPriority = 71;
  if (require(!ardor::hasRequiredRealtimeScheduler(schedulerStats))) return 1;
#endif

  // A failed/unstarted backend must never leave a replacement engine borrowed
  // by a callback. This is the safe no-device result used by the control loop.
  ardor::MiniaudioBackend unstartedBackend;
  ardor::PedalEngine replacementEngine;
  if (require(unstartedBackend.replaceEngine(replacementEngine) == ardor::EngineReplaceResult::DeviceStopped)) return 1;
  if (require(unstartedBackend.deviceStopped())) return 1;

  ardor::ChainPlan cabOnly;
  cabOnly.inputGain = 1.0f;
  cabOnly.outputGain = 0.5f;
  cabOnly.safetyLimit = 1.0f;
  cabOnly.blocks.push_back({"cab-1", "cab", ardor::ChainBlockStatus::Ready, wavPath, nlohmann::json::object()});

  ardor::ChainPlan badNamPreference;
  badNamPreference.blocks.push_back({"nam-1", "nam", ardor::ChainBlockStatus::Ready,
                                     "missing.nam", nlohmann::json{{"useNano", "yes"}}});
  ardor::PedalEngine badNamEngine;
  std::string badNamError;
  if (require(!ardor::applyChainPlan(badNamEngine, badNamPreference, {48000, 64, 8192}, badNamError))) return 1;
  if (require(badNamError.find("must be a boolean") != std::string::npos)) return 1;

  ardor::PedalEngine loadedEngine;
  std::string loadError;
  if (require(ardor::applyChainPlan(loadedEngine, cabOnly, {48000, 64, 8192}, loadError))) return 1;
  auto [loadedLeft, loadedRight] = loadedEngine.process(0.5f);
  // IR {0.25,-0.5} has response peak 0.75 < ceiling, so it is left unchanged:
  // 0.5 in * 0.25 * 0.5 output gain = 0.0625.
  if (require(std::fabs(loadedLeft - 0.0625f) < 0.0001f)) return 1;
  if (require(std::fabs(loadedRight - 0.0625f) < 0.0001f)) return 1;

  auto duplicateCab = cabOnly;
  duplicateCab.blocks.push_back({"cab-2", "cab", ardor::ChainBlockStatus::Ready, wavPath, nlohmann::json::object()});
  if (require(!ardor::applyChainPlan(loadedEngine, duplicateCab, {48000, 64, 8192}, loadError))) return 1;
  if (require(loadError.find("multiple cabinet") != std::string::npos)) return 1;

  ardor::ChainPlan stereoBeforeCab;
  stereoBeforeCab.blocks.push_back({"trem", "mod", ardor::ChainBlockStatus::Ready, {},
                                    nlohmann::json{{"mode", "vintage_trem"}}});
  stereoBeforeCab.blocks.push_back({"cab-after", "cab", ardor::ChainBlockStatus::Ready, wavPath,
                                    nlohmann::json::object()});
  if (require(!ardor::applyChainPlan(loadedEngine, stereoBeforeCab, {48000, 64, 8192}, loadError))) return 1;
  if (require(loadError.find("must precede stereo") != std::string::npos)) return 1;

  if (require(!ardor::applyChainPlan(loadedEngine, cabOnly, {44100, 64, 8192}, loadError))) return 1;
  if (require(loadError.find("48000") != std::string::npos)) return 1;

  ardor::PedalEngine expectedAfterFailedLoad;
  if (require(ardor::applyChainPlan(expectedAfterFailedLoad, cabOnly, {48000, 64, 8192}, loadError))) return 1;
  expectedAfterFailedLoad.process(0.5f);

  ardor::ChainPlan wrongRateCab;
  wrongRateCab.blocks.push_back({"cab-441", "cab", ardor::ChainBlockStatus::Ready, wav441Path, nlohmann::json::object()});
  if (require(!ardor::applyChainPlan(loadedEngine, wrongRateCab, {48000, 64, 8192}, loadError))) return 1;
  if (require(loadError.find("sample rate mismatch") != std::string::npos)) return 1;
  const auto expectedContinuation = expectedAfterFailedLoad.process(0.0f);
  const auto preservedContinuation = loadedEngine.process(0.0f);
  if (require(std::fabs(expectedContinuation.first - preservedContinuation.first) < 0.0001f)) return 1;
  if (require(std::fabs(expectedContinuation.second - preservedContinuation.second) < 0.0001f)) return 1;
  std::filesystem::remove(wav441Path);

  ardor::ChainPlan missingCab;
  missingCab.blocks.push_back({"cab-missing", "cab", ardor::ChainBlockStatus::MissingAsset, {}, nlohmann::json::object()});
  if (require(!ardor::applyChainPlan(loadedEngine, missingCab, {48000, 64, 8192}, loadError))) return 1;
  if (require(loadError.find("cab-missing") != std::string::npos)) return 1;

  const auto badWavPath = std::filesystem::temp_directory_path() / "ardor-bad-wav-smoke.wav";
  {
    std::ofstream badWav(badWavPath);
    badWav << "not a wav";
  }
  ardor::ChainPlan badCab;
  badCab.blocks.push_back({"cab-bad", "cab", ardor::ChainBlockStatus::Ready, badWavPath, nlohmann::json::object()});
  if (require(!ardor::applyChainPlan(loadedEngine, badCab, {48000, 64, 8192}, loadError))) return 1;
  if (require(loadError.find("failed to load IR") != std::string::npos)) return 1;
  std::filesystem::remove(badWavPath);

  loadedEngine.loadIr({0.0f});
  ardor::ChainPlan dryPlan;
  dryPlan.inputGain = 1.0f;
  dryPlan.outputGain = 1.0f;
  dryPlan.safetyLimit = 1.0f;
  if (require(ardor::applyChainPlan(loadedEngine, dryPlan, {48000, 64, 8192}, loadError))) return 1;
  // Loading a new plan into an already-running engine changes gain through the
  // live-control ramp rather than stepping the output.
  for (int i = 0; i < 4096; ++i) {
    loadedEngine.process(0.0f);
  }
  auto [dryLeft, dryRight] = loadedEngine.process(0.5f);
  if (require(std::fabs(dryLeft - 0.5f) < 0.0001f)) return 1;
  if (require(std::fabs(dryRight - 0.5f) < 0.0001f)) return 1;
  std::filesystem::remove(wavPath);

  ardor::IrConvolver ir;
  ir.loadImpulse({1.0f, 0.5f});

  if (require(std::fabs(ir.processSample(1.0f) - 1.0f) < 0.0001f)) return 1;
  if (require(std::fabs(ir.processSample(0.0f) - 0.5f) < 0.0001f)) return 1;
  if (require(std::fabs(ir.processSample(0.0f)) < 0.0001f)) return 1;

  std::vector<float> impulse(192, 0.0f);
  impulse[0] = 0.8f;
  impulse[65] = -0.25f;
  impulse[129] = 0.125f;
  std::vector<float> input(256, 0.0f);
  input[0] = 1.0f;
  input[9] = -0.5f;
  input[140] = 0.25f;

  ardor::IrConvolver naive;
  ardor::IrConvolver fast;
  naive.loadImpulse(impulse);
  fast.loadImpulse(impulse);
  fast.prepareBlockSize(64);

  std::vector<float> expected(input.size(), 0.0f);
  std::vector<float> actual(input.size(), 0.0f);
  for (size_t i = 0; i < input.size(); ++i) {
    expected[i] = naive.processSample(input[i]);
  }
  for (size_t offset = 0; offset < input.size(); offset += 64) {
    fast.processBlock(input.data() + offset, actual.data() + offset, 64);
  }
  for (size_t i = 0; i < input.size(); ++i) {
    // Tolerance tightened 5x with precomputed twiddles; loosening it back is a bug.
    if (require(std::fabs(expected[i] - actual[i]) < 0.0001f)) return 1;
  }

  std::vector<float> mismatchedOutput(32, 1.0f);
  fast.processBlock(input.data(), mismatchedOutput.data(), mismatchedOutput.size());
  if (require(fast.blockSizeMismatchCount() == 1)) return 1;
  for (const float sample : mismatchedOutput) {
    if (require(sample == 0.0f)) return 1;
  }

  ardor::PedalEngine engine;
  engine.loadIr({1.0f});
  engine.setInputGain(0.5f);
  engine.setOutputGain(2.0f);
  const auto [left, right] = engine.process(0.25f);
  if (require(std::fabs(left - 0.25f) < 0.0001f)) return 1;
  if (require(std::fabs(right - 0.25f) < 0.0001f)) return 1;
  engine.setSafetyLimit(0.5f);
  const auto [limitedLeft, limitedRight] = engine.process(1.0f);
  if (require(limitedLeft > 0.475f && limitedLeft < 0.5f)) return 1;
  if (require(limitedRight > 0.475f && limitedRight < 0.5f)) return 1;

  ardor::PedalEngine blockEngine;
  blockEngine.prepareBlockSize(64);
  blockEngine.loadIr(impulse);
  blockEngine.setSafetyLimit(0.5f);
  std::vector<float> blockLeft(input.size(), 0.0f);
  std::vector<float> blockRight(input.size(), 0.0f);
  for (size_t offset = 0; offset < input.size(); offset += 64) {
    blockEngine.processBlock(input.data() + offset, blockLeft.data() + offset, blockRight.data() + offset, 64);
  }
  for (size_t i = 0; i < input.size(); ++i) {
    // The safety stage has a soft knee below its ceiling; it must preserve
    // polarity and keep every output strictly inside the requested limit.
    if (require(std::fabs(blockLeft[i]) < 0.5f && std::fabs(blockRight[i]) < 0.5f)) return 1;
    if (require(expected[i] == 0.0f || std::signbit(expected[i]) == std::signbit(blockLeft[i]))) return 1;
    if (require(expected[i] == 0.0f || std::signbit(expected[i]) == std::signbit(blockRight[i]))) return 1;
  }

  std::vector<float> partialInput(63, 0.25f);
  std::vector<float> partialLeft(partialInput.size(), 1.0f);
  std::vector<float> partialRight(partialInput.size(), 1.0f);
  blockEngine.processBlock(partialInput.data(), partialLeft.data(), partialRight.data(), partialInput.size());
  if (require(blockEngine.blockSizeMismatchCount() == 1)) return 1;
  for (size_t i = 0; i < partialInput.size(); ++i) {
    if (require(partialLeft[i] == 0.0f && partialRight[i] == 0.0f)) return 1;
  }

  if (require(!engine.loadNam(std::filesystem::path{"missing.nam"}, 48000.0, 128))) return 1;
  return 0;
}
