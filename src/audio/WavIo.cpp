#include "audio/WavIo.h"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

namespace ardor {

namespace {

constexpr double kPi = 3.14159265358979323846;

size_t nextPowerOfTwo(size_t value)
{
  size_t out = 1;
  while (out < value) out <<= 1;
  return out;
}

// Iterative radix-2 Cooley-Tukey. Runs once per IR load (offline), so it favors
// clarity over the cached-twiddle path the realtime convolver uses.
void fftInPlace(std::vector<std::complex<float>>& a)
{
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const double angle = -2.0 * kPi / static_cast<double>(len);
    const std::complex<double> wLen(std::cos(angle), std::sin(angle));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0); // double recurrence keeps error negligible over large len
      for (size_t k = 0; k < len / 2; ++k) {
        const auto u = a[i + k];
        const auto v = a[i + k + len / 2] * std::complex<float>(w);
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wLen;
      }
    }
  }
}

// Peak of the magnitude response max|H(f)| = the largest steady-state sinusoidal
// (and L2 energy) gain the convolution applies. This is NOT a bound on output
// sample peaks for arbitrary transient input - that would be sum|h[n]|, which is
// far too conservative for audio; the final limiter still guards transient peaks.
// Bounding max|H(f)| is what keeps a cabinet from adding its 12-15 dB resonance
// on top of the NAM model.
//
// |H(f)| is only evaluated at FFT-bin frequencies, so a peak falling between bins
// is underestimated. Zero-pad by kOversample to make the grid dense enough that
// the residual error is well under the guard margin the caller applies.
constexpr size_t kResponseOversample = 8;

float magnitudeResponsePeak(const std::vector<float>& ir)
{
  const size_t m = nextPowerOfTwo(ir.size() * kResponseOversample);
  std::vector<std::complex<float>> buf(m, std::complex<float>{});
  for (size_t i = 0; i < ir.size(); ++i) {
    buf[i] = ir[i];
  }
  fftInPlace(buf);
  float peak = 0.0f;
  for (size_t k = 0; k <= m / 2; ++k) {
    peak = std::max(peak, std::abs(buf[k]));
  }
  return peak;
}

} // namespace

MonoWav readMonoWav(const std::filesystem::path& path)
{
  // Leave the output channel count native while opening the file. Asking
  // miniaudio for one output channel would otherwise silently downmix a
  // stereo cabinet capture before the engine has a chance to reject it.
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
  ma_decoder decoder;
  if (ma_decoder_init_file(path.string().c_str(), &cfg, &decoder) != MA_SUCCESS) {
    throw std::runtime_error("failed to open wav: " + path.string());
  }

  MonoWav wav;
  ma_format format{};
  ma_uint32 channels = 0;
  ma_uint32 sampleRate = 0;
  if (ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0) != MA_SUCCESS) {
    ma_decoder_uninit(&decoder);
    throw std::runtime_error("failed to inspect wav: " + path.string());
  }
  if (channels != 1) {
    ma_decoder_uninit(&decoder);
    throw std::runtime_error("IR must be mono: " + path.string());
  }
  wav.sampleRate = sampleRate;
  wav.channels = channels;
  float chunk[4096];
  for (;;) {
    ma_uint64 framesRead = 0;
    const ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &framesRead);
    if (result != MA_SUCCESS && result != MA_AT_END) {
      ma_decoder_uninit(&decoder);
      throw std::runtime_error("failed to read wav: " + path.string());
    }
    if (framesRead == 0) break;
    wav.samples.insert(wav.samples.end(), chunk, chunk + framesRead);
  }
  ma_decoder_uninit(&decoder);
  return wav;
}

bool prepareMonoIr(MonoWav& wav, size_t maximumSamples, std::string& error)
{
  error.clear();
  if (wav.channels != 1) {
    error = "IR must be mono";
    return false;
  }
  if (wav.sampleRate == 0) {
    error = "IR sample rate is invalid";
    return false;
  }
  if (wav.samples.empty()) {
    error = "IR contains no audio frames";
    return false;
  }
  if (!std::all_of(wav.samples.begin(), wav.samples.end(), [](float sample) { return std::isfinite(sample); })) {
    error = "IR contains non-finite samples";
    return false;
  }

  if (maximumSamples > 0 && wav.samples.size() > maximumSamples) {
    wav.samples.resize(maximumSamples);
    // A 5 ms tail fade preserves the retained IR while removing the abrupt
    // edge that would otherwise add broadband energy at the truncation point.
    const size_t fadeFrames = std::min(wav.samples.size(),
                                       std::max<size_t>(1, static_cast<size_t>(wav.sampleRate / 200)));
    const size_t fadeStart = wav.samples.size() - fadeFrames;
    for (size_t i = 0; i < fadeFrames; ++i) {
      const float gain = static_cast<float>(fadeFrames - 1 - i) / static_cast<float>(fadeFrames);
      wav.samples[fadeStart + i] *= gain;
    }
  }

  // The level a convolution imposes is its frequency-response peak max|H(f)|,
  // not its peak sample. A cabinet IR concentrates energy in a narrow low-mid
  // resonance, so peak-sample normalization leaves 12-15 dB of gain at that
  // resonance; that stacks on the NAM model's output and clips whenever both
  // blocks are on. Use a guarded -1 dB transfer-gain ceiling so between-bin
  // estimation error cannot leak above the nominal ceiling. Attenuation only: a
  // quiet or near-silent capture is left untouched (never boosted), so a malformed
  // or noise-only IR can never be turned into a full-scale blast. A single scalar
  // preserves the cabinet's response and phase exactly apart from level.
  //
  // Apply the guard to both the decision threshold and the normalized target.
  // This catches estimates that sit just below the nominal ceiling even though
  // their true between-bin peak may sit just above it, and keeps the gain change
  // continuous at the threshold.
  constexpr float kResponseCeiling = 0.8912509f; // -1 dB transfer gain.
  constexpr float kGuardMargin = 0.977f;         // ~0.2 dB safety for between-bin error.
  constexpr float kGuardedCeiling = kResponseCeiling * kGuardMargin;
  const float responsePeak = magnitudeResponsePeak(wav.samples);
  if (responsePeak > kGuardedCeiling) {
    const float gain = kGuardedCeiling / responsePeak;
    for (float& sample : wav.samples) {
      sample *= gain;
    }
  }
  return true;
}

} // namespace ardor
