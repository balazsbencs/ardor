#include "daisyfx/pog3/SpectralFreeze.h"
#include "daisyfx/pog3/PolyphonicPitchBank.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ardor::pog3 {
namespace {
constexpr double kTwoPi = 6.2831853071795864769;
bool freezing(ExpressionMode mode) noexcept { return mode == ExpressionMode::FreezeGliss || mode == ExpressionMode::FreezeVolume; }
float approach(float x, float target, float step) noexcept { return x + std::clamp(target - x, -step, step); }
}
void SpectralFreeze::reset() noexcept {
  latest_ = {}; previous_ = {}; requested_ = {}; held_ = {}; goal_ = {};
  state_ = State::Live; heel_ = true; latched_ = freezing(mode_) && position_ >= .985f;
  ready_ = requestedValid_ = initial_ = targetPending_ = false;
  mix_ = dryMix_ = 0; gain_ = mode_ == ExpressionMode::FreezeVolume ? position_ : 1;
  initialAudibleAt_ = -1; targetAt_ = 0; lastEvent_ = -1920; remaining_ = 0;
  serial_ = captures_ = targets_ = capacityEvents_ = 0;
  // Re-enter a retained nonheel mode through the first-valid-frame rule.
  if (freezing(mode_) && position_ >= .035f) {
    heel_ = false; initial_ = true; state_ = State::CapturePending;
  }
}
bool SpectralFreeze::setControls(ExpressionMode mode, float position, bool dryEligible) noexcept {
  if (!std::isfinite(position) || static_cast<int>(mode) < 0 || static_cast<int>(mode) > 6) return false;
  position = std::clamp(position, 0.0f, 1.0f);
  const bool entering = freezing(mode) && !freezing(mode_);
  if (freezing(mode_) && !freezing(mode)) {
    state_ = mix_ > 0 ? State::Releasing : State::Live;
    requestedValid_ = initial_ = targetPending_ = false; heel_ = true; latched_ = false; remaining_ = 0;
  }
  if (mode_ == ExpressionMode::FreezeGliss && mode == ExpressionMode::FreezeVolume) {
    remaining_ = 0; targetPending_ = false;
    if (state_ == State::Gliding) state_ = State::Held;
  }
  mode_ = mode; position_ = position; dryEligible_ = dryEligible;
  const auto duration = static_cast<std::size_t>(std::lround(.02 * std::pow(150.0, position_) * kSampleRate));
  if (remaining_ && duration_ != duration)
    remaining_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(static_cast<double>(remaining_) * duration / duration_)));
  duration_ = duration;
  if (!freezing(mode)) return true;
  if (entering) heel_ = true;
  if (!heel_ && position <= .015f) {
    heel_ = true; state_ = mix_ > 0 ? State::Releasing : State::Live;
    requestedValid_ = initial_ = targetPending_ = false; remaining_ = 0;
  } else if (heel_ && position >= .035f) {
    heel_ = false; initial_ = !ready_;
    initialAudibleAt_ = -1;
    requestedValid_ = ready_;
    if (ready_) requested_ = latest_; // Most recent compiled frame at the event.
    if (state_ != State::Releasing) state_ = State::CapturePending;
  }
  if (!latched_ && position >= .985f) latched_ = true;
  else if (latched_ && position <= .965f) latched_ = false;
  return true;
}
void SpectralFreeze::capture(FrozenBand& out, const PitchFrame& source, const PartialGains& gains) noexcept {
  out.count = 0; out.center = source.centerSamples();
  const auto spectrum = source.spectrum();
  if (spectrum.empty()) return;
  for (const auto& region : source.regions()) {
    const float frequency = region.frequencyBins * kSampleRate / source.frameSize();
    if (frequency < 20 || frequency >= kSampleRate / 2) continue;
    const float window = source.lobe(static_cast<float>(region.bin) - region.frequencyBins) * spectrum.size();
    if (window < spectrum.size() * .25f) continue;
    auto carrier = spectrum[region.bin] / window;
    if (region.bin & 1) carrier = -carrier;
    const float magnitude = std::abs(carrier) * gains[region.track];
    if (!std::isfinite(magnitude) || magnitude < 1e-9f) continue;
    out.partials[out.count++] = {.center = out.center, .liveGeneration = region.generation,
      .frequency = frequency, .magnitude = magnitude, .phase = std::arg(carrier),
      .liveTrack = static_cast<std::uint16_t>(region.track)};
  }
}
bool SpectralFreeze::onset() const noexcept {
  double positive = 0, total = 0;
  // Linked channel power; low fundamentals and primary high partials do not
  // double-count the crossover. Phase and magnitude remain channel independent.
  for (const auto resolution : {0U, 2U}) for (std::size_t side = 0; side < 2; ++side) {
    const auto& now = latest_[resolution][side];
    const auto& old = previous_[resolution][side];
    for (std::size_t i = 0; i < now.count; ++i) {
      const auto& p = now.partials[i];
      if ((resolution == 0 && p.frequency < 300) || (resolution == 2 && p.frequency >= 300)) continue;
      float prior = 0, best = std::max(2.0f, p.frequency * .02042f);
      for (std::size_t j = 0; j < old.count; ++j) {
        const float distance = std::fabs(old.partials[j].frequency - p.frequency);
        if (distance < best) { best = distance; prior = old.partials[j].magnitude; }
      }
      const double energy = static_cast<double>(p.magnitude) * p.magnitude;
      total += energy;
      positive += std::max(0.0, energy - static_cast<double>(prior) * prior);
    }
  }
  return total > 1e-8 && positive > total * .15;
}
void SpectralFreeze::beginCapture() noexcept {
  held_ = requested_; goal_ = {};
  for (auto& pair : held_) for (auto& band : pair)
    for (std::size_t i = 0; i < band.count; ++i) band.partials[i].id = ++serial_;
  ++captures_; requestedValid_ = initial_ = targetPending_ = false; remaining_ = 0;
  state_ = State::Held;
}
void SpectralFreeze::assignTarget() noexcept {
  // Strongest held partials claim nearest log-frequency targets first; ties
  // preserve source order. Every target is claimed once, all storage is bounded.
  for (std::size_t r = 0; r < 3; ++r) for (std::size_t c = 0; c < 2; ++c) {
    auto& held = held_[r][c]; auto& goal = goal_[r][c];
    const auto& target = latest_[r][c];
    goal.count = held.count;
    for (std::size_t i = 0; i < held.count; ++i)
      goal.partials[i] = {held.partials[i].frequency, held.partials[i].magnitude};
    used_.fill(false);
    // Do not permute held slots: oscillator identity survives each assignment.
    std::array<std::size_t, kMaxPitchPartials> order{};
    for (std::size_t i = 0; i < held.count; ++i) order[i] = i;
    std::sort(order.begin(), order.begin() + held.count, [&](auto a, auto b) {
      return held.partials[a].magnitude == held.partials[b].magnitude ? a < b : held.partials[a].magnitude > held.partials[b].magnitude;
    });
    for (std::size_t rank = 0; rank < held.count; ++rank) {
      const auto i = order[rank];
      if (!held.partials[i].id) { goal.partials[i].magnitude = 0; continue; }
      std::size_t match = target.count; float best = std::numeric_limits<float>::max();
      for (std::size_t j = 0; j < target.count; ++j) if (!used_[j]) {
        const float cost = std::fabs(target.partials[j].frequency - held.partials[i].frequency)
          / std::max(held.partials[i].frequency, target.partials[j].frequency);
        if (cost < best) { best = cost; match = j; }
      }
      if (match == target.count) goal.partials[i].magnitude = 0;
      else { used_[match] = true; goal.partials[i].frequency = target.partials[match].frequency;
             goal.partials[i].magnitude = target.partials[match].magnitude; }
    }
    for (std::size_t j = 0; j < target.count; ++j) if (!used_[j]) {
      std::size_t slot = held.count;
      for (std::size_t i = 0; i < held.count; ++i) if (!held.partials[i].id) { slot = i; break; }
      if (slot == kMaxPitchPartials) { ++capacityEvents_; continue; }
      if (slot == held.count) ++held.count;
      goal.count = held.count;
      held.partials[slot] = target.partials[j];
      goal.partials[slot] = {target.partials[j].frequency, target.partials[j].magnitude};
      held.partials[slot].id = ++serial_;
      held.partials[slot].magnitude = 0;
    }
  }
  ++targets_; targetPending_ = false; remaining_ = duration_; state_ = State::Gliding;
}
void SpectralFreeze::update(const std::array<PitchFrame, 2>& primary,
                           const std::array<PitchFrame, 2>& shortFrames,
                           const std::array<PitchFrame, 2>& low,
                           const StereoAttackGains& primaryGains,
                           const StereoAttackGains& shortGains,
                           const StereoAttackGains& lowGains, std::int64_t inputEnd) noexcept {
  for (std::size_t r = 0; r < 3; ++r) for (std::size_t c = 0; c < 2; ++c) {
    const auto& source = latest_[r][c]; auto& previous = previous_[r][c];
    previous.count = source.count;
    for (std::size_t i = 0; i < source.count; ++i)
      previous.partials[i] = {source.partials[i].frequency, source.partials[i].magnitude};
  }
  for (std::size_t c = 0; c < 2; ++c) {
    capture(latest_[0][c], primary[c], primaryGains[c]);
    capture(latest_[1][c], shortFrames[c], shortGains[c]);
    capture(latest_[2][c], low[c], lowGains[c]);
  }
  // Numerically identical carriers share frequency and transposition offset,
  // preserving stereo phase over indefinite holds. Distinct tones remain
  // independent; this tolerance is only one part per million (or 0.1 mHz).
  for (auto& pair : latest_) for (std::size_t i = 0; i < pair[0].count; ++i) {
    auto& left = pair[0].partials[i];
    for (std::size_t j = 0; j < pair[1].count; ++j) {
      auto& right = pair[1].partials[j];
      if (std::fabs(left.frequency - right.frequency) > std::max(.0001f, left.frequency * .000001f)) continue;
      const float frequency = .5f * (left.frequency + right.frequency);
      left.frequency = right.frequency = frequency;
      right.referenceLeft = true; right.liveTrack = left.liveTrack; right.liveGeneration = left.liveGeneration;
      break;
    }
  }
  // Capture validity requires full history and a second complete low frame.
  // Startup zero padding is valid for streaming, not for stationary pitch.
  ready_ = inputEnd >= 4096 + 512;
  bool audible = false;
  for (const auto& pair : latest_) for (const auto& band : pair) for (std::size_t i = 0; i < band.count; ++i)
    audible |= band.partials[i].magnitude > 1e-5f;
  if (initial_) {
    // A globally warm stream may still contain only zero padding for the new
    // note. Require a complete low window after its first audible partial,
    // including the next low hop, before freezing a stationary carrier.
    if (!audible) initialAudibleAt_ = -1;
    else if (initialAudibleAt_ < 0) initialAudibleAt_ = inputEnd;
    if (ready_ && audible && !heel_ && inputEnd - initialAudibleAt_ >= 4096 + 512) {
      requested_ = latest_; requestedValid_ = true; initial_ = false;
    }
  }
  if (state_ == State::Releasing) {
    mix_ = approach(mix_, 0, 256.0f / 960);
    if (mix_ == 0) {
      held_ = {}; goal_ = {}; remaining_ = 0;
      state_ = !heel_ && freezing(mode_) ? State::CapturePending : State::Live;
    }
  }
  if (state_ == State::CapturePending && requestedValid_) beginCapture();
  if (state_ == State::Held || state_ == State::Gliding) mix_ = approach(mix_, 1, 256.0f / 960);
  dryMix_ = approach(dryMix_, dryEligible_ ? 1 : 0, 256.0f / 960);
  gain_ = approach(gain_, mode_ == ExpressionMode::FreezeVolume ? position_ : 1, 256.0f / 960);
  if (mode_ == ExpressionMode::FreezeGliss && (state_ == State::Held || state_ == State::Gliding)) {
    if (onset() && inputEnd - lastEvent_ >= 1920) {
      lastEvent_ = inputEnd; targetPending_ = true; targetAt_ = inputEnd + 2048;
    }
    if (!latched_ && targetPending_ && inputEnd >= targetAt_) assignTarget();
    if (!latched_ && remaining_) {
      const auto step = std::min<std::size_t>(256, remaining_);
      const float amount = static_cast<float>(step) / remaining_;
      for (std::size_t r = 0; r < 3; ++r) for (std::size_t c = 0; c < 2; ++c) {
        auto& held = held_[r][c]; const auto& goal = goal_[r][c];
        for (std::size_t i = 0; i < held.count; ++i) {
          auto& p = held.partials[i];
          p.frequency += amount * (goal.partials[i].frequency - p.frequency);
          p.magnitude += amount * (goal.partials[i].magnitude - p.magnitude);
          if (step == remaining_) { p.frequency = goal.partials[i].frequency; p.magnitude = goal.partials[i].magnitude;
            if (p.magnitude == 0) p.id = 0; }
        }
      }
      remaining_ -= step;
      if (!remaining_) state_ = State::Held;
    }
  }
}
} // namespace ardor::pog3
