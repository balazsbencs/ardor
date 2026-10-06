#include "daisyfx/pog3/PolyphonicAttack.h"
#include "daisyfx/pog3/PolyphonicPitchBank.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ardor::pog3 {
namespace {
constexpr float kMatch = .02042f; // Approximately 35 cents, never a pitch grid.
constexpr std::int64_t kRefractory = 1920, kRelease = 9600;
float distance(float a, float b) noexcept { return std::fabs(a - b) / std::max(40.0f, b); }
int harmonic(float frequency, float fundamental) noexcept {
  if (fundamental < 40) return 0;
  const float ratio = frequency / fundamental;
  // Only rounded harmonics 1..8 can match. For this positive, bounded
  // interval, truncation plus a fractional comparison is round-to-nearest
  // with half ties away from zero, without a library round call. Do not use
  // int(ratio + .5f): that addition can round a value just below a half tie up.
  if (!(ratio >= .5f && ratio < 8.5f)) return 0;
  const int whole = static_cast<int>(ratio);
  const int n = whole + (ratio - whole >= .5f);
  return n >= 1 && n <= 8 && std::fabs(ratio - n) < n * kMatch ? n : 0;
}
float windowMargin(float frequency, float radius) noexcept {
  // Bounds only prune impossible matches. Leave room for float product/sum
  // rounding; the original strict distance predicate makes the final decision.
  return 8 * std::numeric_limits<float>::epsilon() * (std::fabs(frequency) + radius + 1);
}
}

void PolyphonicAttack::reset() noexcept {
  for (auto& resolution : partials_) resolution = {};
  for (auto& resolution : bindings_) resolution = {};
  families_ = {};
  canonicalCounts_ = {};
  generation_ = capacityEvents_ = 0;
}

bool PolyphonicAttack::setSeconds(float seconds) noexcept {
  if (!std::isfinite(seconds)) return false;
  seconds_ = std::clamp(seconds, 0.0f, 3.0f);
  return true;
}

std::size_t PolyphonicAttack::familyCount() const noexcept {
  return static_cast<std::size_t>(std::count_if(families_.begin(), families_.end(),
    [](const Family& family) { return family.id != 0; }));
}

PolyphonicAttack::Family* PolyphonicAttack::owner(float frequency, std::int64_t inputEnd) noexcept {
  Family* match = nullptr;
  float best = std::numeric_limits<float>::max();
  for (auto& family : families_) {
    if (!family.id || inputEnd - family.seen > kRelease) continue;
    const int n = harmonic(frequency, family.frequency);
    if (!n) continue;
    // Prefer the smallest harmonic number; ties retain the established ID.
    const float score = n + distance(frequency, n * family.frequency);
    if (score < best) { best = score; match = &family; }
  }
  return match;
}

void PolyphonicAttack::group(std::size_t count, std::int64_t inputEnd, std::int64_t onset) noexcept {
  for (auto& family : families_) if (family.id && inputEnd - family.seen > kRelease) family = {};
  // Only directly supported fundamentals are candidates in this first model.
  // This deliberately rejects phantom subharmonic families; missing-fundamental
  // material retains independent residual-partial envelopes.
  std::size_t candidates = 0;
  for (std::size_t p = 0; p < count && candidates < candidates_.size(); ++p) {
    const auto& value = observations_[p];
    if (value.frequency < 40 || value.frequency > 2000 || value.magnitude < 1e-5f) continue;
    bool duplicate = false;
    for (std::size_t c = 0; c < candidates; ++c)
      if (distance(value.frequency, candidates_[c].frequency) < kMatch) { duplicate = true; break; }
    if (duplicate) continue;
    float score = value.magnitude * value.magnitude;
    for (std::size_t q = 0; q < count; ++q) {
      const auto& support = observations_[q];
      const int n = harmonic(support.frequency, value.frequency);
      if (n > 1) score += support.magnitude * support.magnitude / n;
    }
    candidates_[candidates++] = {value.frequency, score, p};
  }
  std::sort(candidates_.begin(), candidates_.begin() + candidates, [](const auto& a, const auto& b) {
    return a.score == b.score ? a.frequency < b.frequency : a.score > b.score;
  });
  for (std::size_t c = 0; c < candidates; ++c) {
    const auto& candidate = candidates_[c];
    if (auto* family = owner(candidate.frequency, inputEnd)) {
      if (distance(candidate.frequency, family->frequency) < kMatch)
        family->frequency = candidate.frequency; // Continuous bends, no semitone rounding.
      family->seen = inputEnd;
      continue;
    }
    auto empty = std::find_if(families_.begin(), families_.end(), [](const auto& value) { return !value.id; });
    if (empty == families_.end()) { ++capacityEvents_; continue; }
    *empty = {++generation_, candidate.frequency, onset, inputEnd};
  }
}

float PolyphonicAttack::envelope(Partial& partial, float magnitude, std::int64_t center,
                                std::int64_t inputEnd, std::int64_t onset, bool birth) noexcept {
  const double duration = seconds_ * kSampleRate;
  if (seconds_ == 0) {
    partial.sustain = magnitude;
    partial.excitation = {};
    partial.magnitude = magnitude;
    partial.onset = onset;
    return 1;
  }
  if (birth) {
    partial.sustain = 0;
    partial.excitation = {};
    partial.excitation[0] = {magnitude, onset};
    partial.onset = onset;
  } else {
    for (auto& excitation : partial.excitation) {
      if (excitation.amplitude && center - excitation.onset >= duration) {
        partial.sustain += excitation.amplitude;
        excitation = {};
      }
    }
    float total = partial.sustain;
    for (const auto& excitation : partial.excitation) total += excitation.amplitude;
    if (magnitude < total && total > 0) {
      const float release = magnitude / total;
      partial.sustain *= release;
      for (auto& excitation : partial.excitation) excitation.amplitude *= release;
    } else if (magnitude > total) {
      const float increment = magnitude - total;
      // A large time-domain re-pluck rises gradually through a Hann window.
      // A high per-hop ratio threshold misses most of that new excitation.
      const bool newEvent = increment > std::max(1e-5f, partial.magnitude * .02f);
      auto recent = std::find_if(partial.excitation.begin(), partial.excitation.end(), [&](const auto& value) {
        return value.amplitude > 0 && inputEnd - value.onset < kRefractory;
      });
      if (recent != partial.excitation.end()) recent->amplitude += increment;
      else if (newEvent) {
        auto empty = std::find_if(partial.excitation.begin(), partial.excitation.end(), [](const auto& value) { return value.amplitude == 0; });
        if (empty != partial.excitation.end()) *empty = {increment, onset};
        else {
          // Bounded fallback: merge into the latest excitation, retaining old
          // sustain and its phase. Never globally restart all partials.
          auto latest = std::max_element(partial.excitation.begin(), partial.excitation.end(),
            [](const auto& a, const auto& b) { return a.onset < b.onset; });
          latest->amplitude += increment;
          ++capacityEvents_;
        }
      } else if (total > 0) {
        const float scale = magnitude / total;
        partial.sustain *= scale;
        for (auto& excitation : partial.excitation) excitation.amplitude *= scale;
      } else partial.sustain += increment;
    }
  }
  partial.magnitude = magnitude;
  return gainAt(partial, center);
}

float PolyphonicAttack::gainAt(const Partial& partial, std::int64_t center) const noexcept {
  if (seconds_ == 0) return 1;
  const double duration = seconds_ * kSampleRate;
  double audible = partial.sustain;
  for (const auto& excitation : partial.excitation) {
    if (excitation.amplitude == 0) continue;
    const double x = std::clamp((center - excitation.onset) / duration, 0.0, 1.0);
    const double ramp = std::sin(1.5707963267948966 * x);
    audible += excitation.amplitude * ramp * ramp;
  }
  return partial.magnitude > 1e-12f ? static_cast<float>(std::clamp(audible / partial.magnitude, 0.0, 1.0)) : 0;
}

void PolyphonicAttack::indexCanonical(std::size_t resolution) noexcept {
  if (resolution == 1) return;
  const auto cache = resolution == 2 ? 1U : 0U;
  auto& sorted = canonicalFrequencies_[cache]; auto& count = canonicalCounts_[cache];
  count = 0;
  for (std::size_t slot = 0; slot < kMaxPitchPartials; ++slot)
    if (partials_[resolution][slot].generation)
      sorted[count++] = {partials_[resolution][slot].frequency, static_cast<std::uint16_t>(slot)};
  std::sort(sorted.begin(), sorted.begin() + count, [](const auto& a, const auto& b) {
    return a.frequency == b.frequency ? a.slot < b.slot : a.frequency < b.frequency;
  });
}

StereoAttackGains PolyphonicAttack::update(const PitchFrame& left, const PitchFrame& right,
                                         std::size_t resolution, std::int64_t inputEnd) noexcept {
  StereoAttackGains gains{};
  if (seconds_ == 0) for (auto& channel : gains) channel.fill(1);
  if (resolution >= kResolutions || !left.frameSize() || left.frameSize() != right.frameSize()) return gains;
  const auto l = left.regions(), r = right.regions();
  rightUsed_.fill(false);
  for (std::size_t p = 0; p < r.size(); ++p)
    rightFrequencies_[p] = {r[p].frequencyBins * kSampleRate / right.frameSize(), static_cast<std::uint16_t>(p)};
  std::sort(rightFrequencies_.begin(), rightFrequencies_.begin() + r.size(), [](const auto& a, const auto& b) {
    return a.frequency == b.frequency ? a.slot < b.slot : a.frequency < b.frequency;
  });
  const float scale = 2.0f / left.frameSize();
  std::size_t count = 0;
  for (const auto& region : l) {
    Observation value{};
    value.frequency = region.frequencyBins * kSampleRate / left.frameSize();
    value.magnitude = region.magnitude * scale;
    value.track[0] = region.track; value.generation[0] = region.generation;
    float best = kMatch;
    std::size_t match = r.size();
    const float radius = kMatch * std::max(40.0f, value.frequency);
    const float margin = windowMargin(value.frequency, radius);
    auto candidate = std::lower_bound(rightFrequencies_.begin(), rightFrequencies_.begin() + r.size(),
      value.frequency - radius - margin, [](const auto& entry, float frequency) { return entry.frequency < frequency; });
    for (; candidate != rightFrequencies_.begin() + r.size() && candidate->frequency <= value.frequency + radius + margin; ++candidate) {
      const auto p = candidate->slot;
      if (rightUsed_[p]) continue;
      const float d = distance(candidate->frequency, value.frequency);
      if (d < best || (match != r.size() && d == best && p < match)) { best = d; match = p; }
    }
    if (match != r.size()) {
      const auto& other = r[match];
      const float rightMagnitude = other.magnitude * scale;
      value.frequency = (value.frequency * value.magnitude + other.frequencyBins * kSampleRate / right.frameSize() * rightMagnitude)
        / std::max(value.magnitude + rightMagnitude, 1e-12f);
      value.magnitude = static_cast<float>(std::sqrt(static_cast<double>(value.magnitude) * value.magnitude
        + static_cast<double>(rightMagnitude) * rightMagnitude));
      value.track[1] = other.track; value.generation[1] = other.generation;
      rightUsed_[match] = true;
    }
    observations_[count++] = value;
  }
  for (std::size_t p = 0; p < r.size(); ++p) if (!rightUsed_[p]) {
    const auto& region = r[p];
    Observation value{};
    value.frequency = region.frequencyBins * kSampleRate / right.frameSize();
    value.magnitude = region.magnitude * scale;
    value.track[1] = region.track; value.generation[1] = region.generation;
    observations_[count++] = value;
  }
  std::sort(observations_.begin(), observations_.begin() + count, [](const auto& a, const auto& b) {
    return a.magnitude == b.magnitude ? a.frequency < b.frequency : a.magnitude > b.magnitude;
  });
  if (count > kMaxPitchPartials) { count = kMaxPitchPartials; ++capacityEvents_; }
  // Frame-end onset dates refer to input samples, not callback or output time.
  constexpr std::array<std::int64_t, kResolutions> hops{256, 128, 512};
  const auto onset = std::max<std::int64_t>(0, inputEnd - hops[resolution]);
  if (seconds_ > 0) group(count, inputEnd, onset);
  used_.fill(false);
  auto& partials = partials_[resolution];
  auto& bindings = bindings_[resolution];
  // Reserve every surviving binding before allocating births. Otherwise a
  // louder new observation could steal an old sustain's not-yet-visited slot.
  reserved_.fill(false);
  for (std::size_t p = 0; p < count; ++p) for (std::size_t channel = 0; channel < 2; ++channel) {
    const auto& value = observations_[p];
    if (value.track[channel] == kMaxPitchPartials) continue;
    const auto& binding = bindings[channel][value.track[channel]];
    if (binding.source == value.generation[channel] && binding.generation
        && binding.generation == partials[binding.slot].generation) reserved_[binding.slot] = true;
  }
  for (std::size_t p = 0; p < count; ++p) {
    const auto& value = observations_[p];
    std::size_t slot = partials.size();
    for (std::size_t channel = 0; channel < 2; ++channel) {
      if (value.track[channel] == kMaxPitchPartials) continue;
      const auto& binding = bindings[channel][value.track[channel]];
      if (binding.source == value.generation[channel] && binding.generation == partials[binding.slot].generation
          && binding.generation && !used_[binding.slot]) { slot = binding.slot; break; }
    }
    const bool birth = slot == partials.size();
    if (birth) {
      std::int64_t oldest = std::numeric_limits<std::int64_t>::max();
      for (std::size_t s = 0; s < partials.size(); ++s)
        if (!used_[s] && !reserved_[s] && (!partials[s].generation || partials[s].seen < oldest)) {
          slot = s; oldest = partials[s].seen;
          if (!partials[s].generation) break;
        }
      if (slot == partials.size()) { ++capacityEvents_; continue; }
      partials[slot] = {}; partials[slot].generation = ++generation_;
    }
    auto& partial = partials[slot];
    auto* family = owner(value.frequency, inputEnd);
    auto sharedOnset = onset;
    if (family) {
      partial.family = family->id;
      family->seen = inputEnd;
      if (birth && inputEnd - family->onset < kRefractory) sharedOnset = family->onset;
    }
    partial.frequency = value.frequency; partial.seen = inputEnd;
    float gain = envelope(partial, value.magnitude, left.centerSamples(), inputEnd, sharedOnset, birth);
    // Short windows exhibit beat-driven peak-magnitude changes even for a held
    // chord. Use the resolved low/long excitation history at this frame's input
    // timestamp, rather than treating each short-window beat as a fresh pluck.
    // This also gives the two Focus paths the same absolute attack age.
    if (resolution != 2 && seconds_ > 0) {
      const auto source = value.frequency < 400 ? 2U : 0U;
      if (source != resolution) {
        const Partial* match = nullptr;
        float best = std::max(value.frequency * kMatch, kSampleRate / static_cast<float>(left.frameSize()));
        const auto cache = source == 2 ? 1U : 0U;
        const auto& sorted = canonicalFrequencies_[cache];
        const auto end = sorted.begin() + canonicalCounts_[cache];
        const float margin = windowMargin(value.frequency, best);
        // Keep the initial window fixed while the original nearest predicate
        // shrinks best. Ties still choose the lowest original partial slot.
        const float maximum = value.frequency + best + margin;
        auto candidate = std::lower_bound(sorted.begin(), end, value.frequency - best - margin,
          [](const auto& entry, float frequency) { return entry.frequency < frequency; });
        std::size_t matchSlot = kMaxPitchPartials;
        for (; candidate != end && candidate->frequency <= maximum; ++candidate) {
          const auto& canonical = partials_[source][candidate->slot];
          if (!canonical.generation || inputEnd - canonical.seen > kRelease) continue;
          const float d = std::fabs(value.frequency - canonical.frequency);
          if (d < best || (match && d == best && candidate->slot < matchSlot)) {
            best = d; match = &canonical; matchSlot = candidate->slot;
          }
        }
        if (match) gain = gainAt(*match, left.centerSamples());
      }
    }
    used_[slot] = true;
    for (std::size_t channel = 0; channel < 2; ++channel) if (value.track[channel] != kMaxPitchPartials) {
      bindings[channel][value.track[channel]] = {value.generation[channel], partial.generation, slot};
      gains[channel][value.track[channel]] = gain;
    }
  }
  indexCanonical(resolution);
  return gains;
}

} // namespace ardor::pog3
