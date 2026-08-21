#pragma once

#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/hosted/dsp/dc_blocker.h"
#include "daisyfx/hosted/dsp/halfband_resampler.h"
#include "equalizer/ParametricEqMath.h"
#include "tape/TapeHysteresis.h"
#include "tape/TapeTransport.h"

#include <array>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>

namespace ardor {

// A studio tape machine, voiced after a Studer A800.
//
// Signal path, per sample:
//
//   drive trim -> record pre-emphasis -> [8x: Jiles-Atherton] -> de-emphasis
//   -> bias loss -> head bump -> transport -> hiss -> output trim -> DC block
//   -> mix against the latency-matched dry signal
//
// Two things about this machine are commonly got wrong, so they are stated
// here rather than left to be re-derived.
//
// Speed does not switch a treble filter in. With a playback gap near 2 um at
// 15 ips the first gap-loss null lands around 190 kHz, and an A800 is flat to
// 20 kHz at both speeds. What speed really changes is the head bump — about
// +2.5 dB near 45 Hz at 15 ips against a smaller one near 90 Hz at 30 ips —
// and the IEC emphasis constant, 35 us against 17.5 us.
//
// Bias is the one phenomenological control here. Real AC bias is an oscillator
// above 100 kHz; the internal Nyquist is 192 kHz and the anti-imaging
// halfbands do not pass anything near it, so simulating it would produce
// aliasing rather than realism. Bias instead maps to what bias controls: the
// coercivity, and an over-bias high-frequency loss.
class TapeProcessor {
public:
  TapeProcessor() = default;
  TapeProcessor(TapeProcessor&&) noexcept = default;
  TapeProcessor& operator=(TapeProcessor&&) noexcept = default;

  bool configure(const nlohmann::json& params, float sampleRate, std::string& error);
  bool setParameterTarget(const std::string& key, float value);
  void reset();
  StereoSample process(StereoSample input);

  // Exactly this many frames, not approximately. See kWetTrim below.
  std::size_t latencyFrames() const noexcept { return kBlockLatency; }

private:
  TapeProcessor(const TapeProcessor&) = delete;
  TapeProcessor& operator=(const TapeProcessor&) = delete;

  struct Biquad {
    BiquadCoefficients coefficients{};
    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;

    float process(float x)
    {
      const float y = coefficients.b0 * x + coefficients.b1 * x1 + coefficients.b2 * x2
                    - coefficients.a1 * y1 - coefficients.a2 * y2;
      x2 = x1;
      x1 = x;
      y2 = y1;
      y1 = y;
      return y;
    }

    void clear() { x1 = x2 = y1 = y2 = 0.0f; }
  };

  static constexpr std::size_t kOversampling = 8;

  // Six halfband stages cost 15 samples each at 96, 192, 384, 384, 192 and
  // 96 kHz: 7.5 + 3.75 + 1.875 + 1.875 + 3.75 + 7.5 = 26.25 frames at 48 kHz.
  static constexpr float kHalfbandLatency = 26.25f;

  // A quarter of a sample is not a rounding detail. Left alone, the block's
  // latency would be 26.25 + 48 = 74.25 frames, the dry path could only be read
  // at an integer offset, and Mix would blend two signals a quarter sample
  // apart — which at 1.3 kHz is already a 4% amplitude error, about -27 dB.
  //
  // So the wet path is padded to the next whole frame. The pad is 1.75 rather
  // than the 0.75 the arithmetic suggests, because a four-point Lagrange
  // cannot realise a delay below one sample causally: its outermost tap would
  // have to come from the future. Its usable range is [1, 2], so the smallest
  // pad that lands on a whole number is 1.75.
  //
  // Total wet latency is then 26.25 + 1.75 + 48 = 76 frames exactly, the dry
  // read is a plain integer index, Mix is exact at both ends, and
  // latencyFrames() is honest rather than rounded.
  static constexpr float kWetTrim = 1.75f;
  static constexpr std::size_t kBlockLatency =
    static_cast<std::size_t>(kHalfbandLatency + kWetTrim) + TapeTransport::kNominalDelay;

  // Fixed fractional delay for the wet pad. Third-order Lagrange with
  // coefficients computed once, because the fraction never moves.
  struct WetTrim {
    static constexpr std::size_t kSize = 8;
    static constexpr std::size_t kMask = kSize - 1U;
    std::array<float, kSize> history{};
    std::size_t write = 0;

    float process(float x)
    {
      history[write] = x;
      write = (write + 1U) & kMask;

      // The newest sample sits at write-1. Place the four taps at write-4,
      // write-3, write-2 and write-1, which map to Lagrange positions
      // -1, 0, +1 and +2. Delay at position p is 2 - p, so a delay of 1.75
      // means evaluating at p = 0.25 — inside the middle interval, where a
      // four-point Lagrange is well behaved.
      const float y0 = history[(write + kSize - 4U) & kMask];
      const float y1 = history[(write + kSize - 3U) & kMask];
      const float y2 = history[(write + kSize - 2U) & kMask];
      const float y3 = history[(write + kSize - 1U) & kMask];

      constexpr float f = 2.0f - kWetTrim;
      constexpr float c0 = -f * (f - 1.0f) * (f - 2.0f) / 6.0f;
      constexpr float c1 = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f;
      constexpr float c2 = -(f + 1.0f) * f * (f - 2.0f) / 2.0f;
      constexpr float c3 = (f + 1.0f) * f * (f - 1.0f) / 6.0f;
      return y0 * c0 + y1 * c1 + y2 * c2 + y3 * c3;
    }

    void clear()
    {
      history.fill(0.0f);
      write = 0;
    }
  };

  // One oversampled, hysteresis-bearing lane. The machine is stereo, so there
  // are two, and each carries its own resamplers and solver state.
  struct Lane {
    pedal::HalfbandInterpolator2x up2x;
    pedal::HalfbandInterpolator2x up4x;
    pedal::HalfbandInterpolator2x up8x;
    pedal::HalfbandDecimator2x down4x;
    pedal::HalfbandDecimator2x down2x;
    pedal::HalfbandDecimator2x down1x;
    TapeHysteresis core;
    Biquad preEmphasis;
    Biquad deEmphasis;
    Biquad biasLoss;
    Biquad bumpPeak;
    Biquad bumpDip;
    WetTrim trim;
    pedal::DcBlocker dc;
  };

  static constexpr std::size_t kDryBufferSize = 128; // power of two, masked index
  static constexpr std::size_t kDryBufferMask = kDryBufferSize - 1U;

  void rebuildFilters(bool resetMagnetics = false);
  void calibrateDriveMakeup();
  float driveMakeup(float driveDb) const;
  TapeHysteresis::Parameters solverParameters() const;
  float processLane(Lane& lane, float input);
  float readDry(const std::array<float, kDryBufferSize>& buffer) const;

  float sampleRate_ = 48000.0f;
  float smoothing_ = 0.0f;
  bool fastSpeed_ = false; // false is 15 ips, true is 30 ips

  float driveDbTarget_ = 0.0f;
  float saturationTarget_ = 0.5f;
  float biasTarget_ = 0.5f;
  float headBumpTarget_ = 0.5f;
  float mixTarget_ = 1.0f;
  float outputTarget_ = 1.0f;

  float driveDb_ = 0.0f;
  float mix_ = 1.0f;
  float output_ = 1.0f;
  float driveGain_ = 1.0f;
  float makeup_ = 1.0f;

  // Makeup measured at configure() time by running a probe tone through a
  // scratch copy of the magnetics at each of these drive settings. Nothing
  // here is hand-tuned; interpolating between measurements keeps drive
  // changes level-steady without putting a calibration sweep on the UI path.
  static constexpr std::size_t kCalibrationPoints = 13;
  static constexpr float kCalibrationMinDb = -12.0f;
  static constexpr float kCalibrationMaxDb = 24.0f;
  std::array<float, kCalibrationPoints> makeupTable_{};

  Lane left_{};
  Lane right_{};
  TapeTransport transport_{};

  std::array<float, kDryBufferSize> dryLeft_{};
  std::array<float, kDryBufferSize> dryRight_{};
  std::size_t dryWrite_ = 0;
};

} // namespace ardor
