#include "daisyfx/pog3/FrameBirthSlots.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
struct Track { std::uint64_t generation = 0; unsigned missed = 0; };
// Original two scans, independent of the candidate's buckets and cursors.
template<std::size_t N>
std::size_t original(const std::array<Track, N>& tracks, const std::array<bool, N>& used) {
  for (std::size_t t = 0; t < N; ++t)
    if (!used[t] && (!tracks[t].generation || tracks[t].missed > 4)) return t;
  std::size_t match = N;
  unsigned oldest = 0;
  for (std::size_t t = 0; t < N; ++t)
    if (!used[t] && tracks[t].missed > oldest) { oldest = tracks[t].missed; match = t; }
  return match;
}
template<std::size_t N>
void compare(ardor::pog3::detail::FrameBirthSlots<N>& slots, std::array<Track, N> tracks,
             std::array<bool, N> used, std::mt19937* random, std::size_t& checked) {
  slots.prepare(tracks);
  for (std::size_t step = 0; step < N + 2; ++step) {
    // Surviving predictions can select intervening slots. Only selected keys
    // mutate, and an unavailable slot never becomes available within a frame.
    if (random && step % 3 == 0) {
      const auto t = (*random)() % N;
      used[t] = true; tracks[t] = {1, 0};
    }
    const auto expected = original(tracks, used), actual = slots.take(used);
    ++checked;
    if (actual != expected) throw std::runtime_error("frame birth selection changed");
    if (actual < N) { used[actual] = true; tracks[actual] = {1, 0}; }
  }
}
}
int main() {
  try {
    std::size_t checked = 0;
    ardor::pog3::detail::FrameBirthSlots<4> small;
    // Exhaust every four-slot arrangement of empty or generated ages 0..5,
    // and every initial used mask. Repeated prepare also tests cursor renewal.
    for (unsigned scenario = 0; scenario < 2401; ++scenario) {
      std::array<Track, 4> tracks{};
      unsigned keys = scenario;
      for (auto& track : tracks) {
        const auto key = keys % 7; keys /= 7;
        track = key ? Track{1, key - 1} : Track{0, 0};
      }
      for (unsigned mask = 0; mask < 16; ++mask) {
        std::array<bool, 4> used{};
        for (unsigned i = 0; i < 4; ++i) used[i] = mask & (1U << i);
        compare(small, tracks, used, nullptr, checked);
      }
    }
    std::mt19937 random(0x4652414d);
    ardor::pog3::detail::FrameBirthSlots<256> slots;
    for (unsigned scenario = 0; scenario < 2048; ++scenario) {
      std::array<Track, 256> tracks{};
      std::array<bool, 256> used{};
      for (std::size_t i = 0; i < tracks.size(); ++i) {
        tracks[i] = {random() % 5 ? 1U : 0U, static_cast<unsigned>(random() % 6)};
        used[i] = random() % 7 == 0;
        if (scenario == 0) { tracks[i] = {0, static_cast<unsigned>(i % 6)}; used[i] = false; }
        if (scenario == 1) { tracks[i] = {1, 0}; used[i] = false; }
        if (scenario == 2) { tracks[i] = {1, 4}; used[i] = false; }
        if (scenario == 3) { tracks[i] = {i % 2, 5}; used[i] = false; }
        if (scenario == 4) used[i] = true;
      }
      compare(slots, tracks, used, scenario >= 5 ? &random : nullptr, checked);
    }
    std::cout << "Frame birth slots: " << checked
              << " selections equal original scans; exhaustive bounded ages, combined empty/expired order, ties, intervening uses, exhaustion and fresh-frame cursors passed.\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
