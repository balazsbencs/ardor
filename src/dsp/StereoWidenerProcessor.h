#pragma once

#include "daisyfx/DaisyFxProcessor.h"

#include <cstddef>
#include <vector>

namespace ardor {

// Mid/side width, with the low end kept centred and an optional side delay.
//
// Everything upstream in this chain is mono-in — an amp model and a cabinet —
// so by the time a signal reaches the modulation and reverb blocks its width is
// whatever those blocks invented. This gives that a control at the end of the
// chain.
//
// Two deliberate choices about mono compatibility:
//
// Width scales the side component, so a mono fold-down is unaffected by it: the
// side cancels in the sum whatever its gain. Turning width up cannot quietly
// change how the rig sounds through a single speaker.
//
// The delay is applied to the side component rather than to one channel. A
// classic Haas trick delays a whole channel, which combs badly when summed; a
// side-only delay widens without touching the mono sum at all.
class StereoWidenerProcessor {
public:
  static constexpr float MAX_DELAY_MS = 30.0f;

  bool prepare(float sampleRate, std::string& error);
  void reset();

  void setWidth(float width);          // 0 = mono, 1 = unchanged, 2 = double
  void setDelayMs(float milliseconds); // on the side component only
  void setBassMonoHz(float hz);        // below this, the image is centred
  void setLevelDb(float levelDb);

  StereoSample process(StereoSample input);

private:
  float sampleRate_ = 48000.0f;

  std::vector<float> sideDelay_;
  std::size_t write_ = 0;
  std::size_t delaySamples_ = 0;

  // One pole splits the side into a low part that gets centred and a high part
  // that keeps its width.
  float bassState_ = 0.0f;
  float bassCoeff_ = 0.0f;
  bool  bassMonoActive_ = false;

  float widthTarget_ = 1.0f;
  float width_ = 1.0f;
  float levelTarget_ = 1.0f;
  float level_ = 1.0f;
};

} // namespace ardor
