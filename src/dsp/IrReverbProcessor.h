#pragma once

#include "daisyfx/DaisyFxProcessor.h"
#include "dsp/NonUniformConvolver.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ardor {

struct IrReverbLiveParameters;

// True convolution reverb.
//
// Long-tail accumulation is spread across large scheduled partitions instead
// of arriving as one callback-boundary burst. The first 1024 taps use 128-frame
// partitions so initial reflections begin after 2.67 ms at 48 kHz; the rest use
// 1024-frame partitions to keep the steady-state cost low. Only the
// nonredundant half of each real-input spectrum is stored and multiplied.
//
// The partition delay lands on the wet path only; the dry passes straight
// through. On a reverb that reads as pre-delay rather than as latency, which is
// why it is reported through preDelayFrames() and folded into the displayed
// pre-delay instead of being handed to the host for compensation. Compensating
// it would delay the dry signal to match, which is the opposite of wanted.
class IrReverbProcessor {
public:
  // Impulses longer than this are truncated. At 48 kHz a 4 s stereo impulse
  // uses about 8 MB across stereo impulse and history storage.
  static constexpr float MAX_IMPULSE_SECONDS = 4.0f;
  static constexpr std::size_t PARTITION_FRAMES =
      NonUniformConvolver::EARLY_PARTITION_FRAMES;
  // Ends of the wet tone controls; at these values the filter is bypassed.
  static constexpr float LOW_CUT_MIN_HZ = 20.0f;
  static constexpr float LOW_CUT_MAX_HZ = 2000.0f;
  static constexpr float HIGH_CUT_MIN_HZ = 500.0f;
  static constexpr float HIGH_CUT_MAX_HZ = 20000.0f;

  // `right` may be empty, in which case `left` feeds both channels.
  bool load(std::vector<float> left, std::vector<float> right, float sampleRate,
            std::string& error);
  void reset();

  void setMix(float mix);              // 0..1
  void setLevelDb(float levelDb);
  void setPreDelayMs(float milliseconds);
  void setLowCutHz(float hz);          // high-pass on the wet path
  void setHighCutHz(float hz);         // low-pass on the wet path

  StereoSample process(StereoSample input);

  // Delay the wet path carries before any pre-delay is added, in frames.
  std::size_t preDelayFrames() const noexcept { return PARTITION_FRAMES; }
  std::size_t tailFrames() const noexcept;
  bool loaded() const noexcept { return loaded_; }

private:
  struct OnePole {
    float state = 0.0f;
    float coeff = 1.0f;
    void reset() { state = 0.0f; }
    float lowPass(float x) { state += coeff * (x - state); return state; }
    float highPass(float x) { state += coeff * (x - state); return x - state; }
  };

  void updateFilters();
  void refreshLiveParameters() noexcept;

  NonUniformConvolver left_;
  NonUniformConvolver right_;
  bool loaded_ = false;
  float sampleRate_ = 48000.0f;
  std::size_t impulseFrames_ = 0;

  // Pre-delay sits ahead of the convolver, so its buffer only needs to cover
  // the additional delay the user asks for.
  std::vector<float> preLeft_;
  std::vector<float> preRight_;
  std::size_t preWrite_ = 0;
  std::size_t preDelaySamples_ = 0;

  OnePole lowCutL_, lowCutR_, highCutL_, highCutR_;
  float lowCutHz_ = LOW_CUT_MIN_HZ;
  float highCutHz_ = HIGH_CUT_MAX_HZ;
  // At the ends of their travel these controls are off, not merely gentle. A
  // one-pole low-pass at 20 kHz still costs a few percent of a transient, so
  // leaving it in circuit would mean the tail never matches the impulse it was
  // loaded from.
  bool lowCutActive_ = false;
  bool highCutActive_ = false;

  float mixTarget_ = 0.35f;
  float mix_ = 0.35f;
  float levelTarget_ = 1.0f;
  float level_ = 1.0f;
  std::shared_ptr<IrReverbLiveParameters> liveParameters_;
  std::uint64_t liveRevision_ = 0;
};

} // namespace ardor
