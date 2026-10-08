#include "erb_attack_freeze.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
constexpr double tau = 2 * std::numbers::pi;
using Bank = pog3_trial::ErbAttackFreezeBank;
using Mode = ardor::pog3::ExpressionMode;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
double amplitude(const std::vector<float>& x, double hz, std::size_t start, std::size_t count) {
  std::complex<double> sum{}; double weights = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const double w = .5 - .5 * std::cos(tau * i / count);
    sum += static_cast<double>(x[start + i]) * w * std::polar(1.0, -tau * hz * i / 48000);
    weights += w;
  }
  return 2 * std::abs(sum) / weights;
}
double db(double a, double b) { return 20 * std::log10(std::max(a / b, 1e-30)); }

void attackOff() {
  pog3_trial::ErbCadenceBank<32, true> reference;
  auto bank = std::make_unique<Bank>(Bank::Stage::Attack);
  bank->setAttack(0);
  for (std::size_t i = 0; i < 10000; ++i) {
    if (i % 48 == 0) {
      const double extent = (i / 48 % 17) / 16.0;
      reference.setWarp(extent); bank->transpose(extent);
    }
    const float x = .1 * std::sin(tau * 329.628 * i / 48000);
    require(bank->process({x, -.73f * x}) == reference.process({x, -.73f * x}), "Attack off retains exact cadence output");
  }
  require(bank->ownership().transforms() > 0, "all ownership analysis remains active");
  bank->reset();
  for (std::size_t i = 0; i < 5000; ++i) require(bank->process({}) == Bank::Frame{}, "reset clears live and held histories");
}

void attackNotes(double oldHz, double newHz, bool enforce) {
  auto reference = std::make_unique<Bank>(Bank::Stage::Ownership);
  auto attack = std::make_unique<Bank>(Bank::Stage::Attack);
  attack->setAttack(.5f);
  std::array<std::vector<float>, 8> raw, swell, rawRight, swellRight;
  for (auto& x : raw) x.resize(96000);
  for (auto& x : swell) x.resize(96000);
  for (auto& x : rawRight) x.resize(96000);
  for (auto& x : swellRight) x.resize(96000);
  double anti = 0;
  for (std::size_t i = 0; i < 96000; ++i) {
    double x = .1 * std::sin(tau * oldHz * i / 48000);
    if (i >= 48000) x += .08 * std::sin(tau * newHz * i / 48000);
    const std::array<float, 2> input{static_cast<float>(x), static_cast<float>(-.73 * x)};
    const auto a = reference->process(input), b = attack->process(input);
    for (std::size_t v = 0; v < 8; ++v) {
      raw[v][i] = a[v][0]; swell[v][i] = b[v][0];
      rawRight[v][i] = a[v][1]; swellRight[v][i] = b[v][1];
      require(std::isfinite(b[v][0]) && std::isfinite(b[v][1]), "finite Attack output");
      if (v == 0) anti = std::max(anti, std::fabs(b[v][1] + .73 * b[v][0]));
    }
  }
  std::cout << "Attack anti-phase deviation=" << anti << '\n';
  if (enforce) require(anti < 2e-5, "resolved processed unison retains unequal anti-phase stereo");
  constexpr std::array<double, 8> semitones{0, -24, -12, 7, 12, 24, 12, 24};
  for (std::size_t v = 0; v < 8; ++v) {
    const double ratio = std::exp2(semitones[v] / 12);
    // This projection is not sufficiently separated for a close-low pair;
    // that case is logged as a limitation, not certified by these numbers.
    const double held = db(amplitude(swell[v], oldHz * ratio, 49536, 4800), amplitude(raw[v], oldHz * ratio, 49536, 4800));
    const double early = db(amplitude(swell[v], newHz * ratio, 49536, 2400), amplitude(raw[v], newHz * ratio, 49536, 2400));
    const double late = db(amplitude(swell[v], newHz * ratio, 80000, 12000), amplitude(raw[v], newHz * ratio, 80000, 12000));
    std::cout << "Attack input=" << oldHz << '/' << newHz << " voice=" << v << " held/early/late dB=" << held << '/' << early << '/' << late << '\n';
    const double rightEarly = db(amplitude(swellRight[v], newHz * ratio, 49536, 2400), amplitude(rawRight[v], newHz * ratio, 49536, 2400));
    std::cout << "Attack early projection L/R difference=" << early - rightEarly << " dB\n";
    if (enforce && v == 0) require(std::fabs(early - rightEarly) < .05, "processed-unison Attack attenuation remains linked across channels");
    if (enforce) {
      require(std::fabs(held) < 1, "resolved new note does not duck established note by 1 dB");
      require(early < -10, "resolved new note early suppression exceeds 10 dB");
      require(std::fabs(late) < 1, "resolved note settles within 1 dB");
    }
  }
}

struct HeldSource {
  std::array<std::array<ardor::pog3::FrozenBand, 2>, 3> bands{};
  const auto& band(std::size_t r, std::size_t c) const { return bands[r][c]; }
};
void heldNumerical(std::size_t seconds) {
  auto renderer = std::make_unique<pog3_trial::ErbHeldRenderer>();
  HeldSource source;
  const pog3_trial::ErbPhaseVoices::Ratios ratios{1, .25, .5, std::exp2(7.0 / 12), 2, 4, 2, 4};
  for (auto& pair : source.bands) for (auto& b : pair) {
    b.count = 1; b.partials[0] = {.id = 1, .frequency = 317.173f, .magnitude = .1f, .phase = .31f};
  }
  double worst = 0;
  for (std::size_t i = 0; i < seconds * 48000; ++i) {
    const auto y = renderer->process(source, ratios, i);
    if (i < 32) continue;
    for (std::size_t v = 0; v < 8; ++v) {
      const double expected = .2 * std::cos(static_cast<double>(source.bands[0][0].partials[0].phase)
        + tau * source.bands[0][0].partials[0].frequency * ratios[v] * i / 48000);
      worst = std::max(worst, std::fabs(y[v][0] - expected));
      require(y[v][0] == y[v][1], "identical held source stereo retains identical recurrence");
    }
  }
  std::cout << "Held recurrence seconds=" << seconds << " worst absolute error=" << worst << '\n';
  require(worst < 4e-6, "independently known held carrier trajectory");
  renderer->reset(); source.bands = {};
  for (std::size_t i = 0; i < 100; ++i) require(renderer->process(source, ratios, i) == Bank::Frame{}, "held reset silence");
}

void heldCaptureCenters() {
  auto renderer = std::make_unique<pog3_trial::ErbHeldRenderer>();
  HeldSource source;
  const float hz = 259.3f;
  constexpr std::array<std::int64_t, 3> centers{9973, 10357, 8921};
  constexpr std::size_t capture = 20000;
  const pog3_trial::ErbPhaseVoices::Ratios ratios{1, .25, .5, std::exp2(7.0 / 12), 2, 4, 2, 4};
  for (std::size_t r = 0; r < 3; ++r) for (auto& b : source.bands[r]) {
    b.count = 1; b.partials[0] = {.center = centers[r], .id = 1, .frequency = hz, .magnitude = .1f,
      .phase = static_cast<float>(std::remainder(.31 + tau * hz * centers[r] / 48000, tau))};
  }
  double worst = 0;
  for (std::size_t i = 0; i < 48000; ++i) {
    const auto y = renderer->process(source, ratios, capture + i);
    if (i < 32) continue;
    for (std::size_t v = 0; v < 8; ++v) {
      const double expected = .2 * std::cos(.31 + tau * hz * capture / 48000 + tau * hz * ratios[v] * i / 48000);
      worst = std::max(worst, std::fabs(y[v][0] - expected));
    }
  }
  std::cout << "Held different-window-center worst error=" << worst << '\n';
  require(worst < 4e-6, "different capture centers extrapolate input phase before shifting");
}

void heldMovingAndAlias() {
  auto renderer = std::make_unique<pog3_trial::ErbHeldRenderer>();
  HeldSource source;
  for (auto& pair : source.bands) for (auto& b : pair) {
    b.count = 1; b.partials[0] = {.id = 1, .frequency = 259.3f, .magnitude = .1f, .phase = .31f};
  }
  pog3_trial::ErbPhaseVoices::Ratios ratios{1, .25, .5, std::exp2(7.0 / 12), 2, 4, 2, 4};
  std::array<double, 8> phase{}, steps{}; phase.fill(.31f);
  double worst = 0;
  for (std::size_t i = 0; i < 48000; ++i) {
    if (i % 48 == 0) {
      const double extent = .25 + .75 * (i / 48 % 17) / 16;
      constexpr std::array<double, 8> semitones{0, -24, -12, 7, 12, 24, 12, 24};
      for (std::size_t v = 0; v < 8; ++v) ratios[v] = std::exp2(semitones[v] * (v >= 6 ? 1 : extent) / 12);
    }
    if (i % 256 == 0) for (auto& pair : source.bands) for (auto& b : pair)
      b.partials[0].frequency = 259.3f + 90 * std::sin(i * .00043);
    if (i % 32 == 0) for (std::size_t v = 0; v < 8; ++v)
      steps[v] = tau * source.bands[0][0].partials[0].frequency * ratios[v] / 48000;
    const auto y = renderer->process(source, ratios, i);
    for (std::size_t v = 0; v < 8; ++v) {
      if (i >= 32) worst = std::max(worst, std::fabs(y[v][0] - .2 * std::cos(phase[v])));
      phase[v] = std::remainder(phase[v] + steps[v], tau);
    }
  }
  std::cout << "Held endpoint-quantized moving phase worst error=" << worst << '\n';
  require(worst < 4e-6, "held moving frequency/Warp integrates phase without reseeding IDs");
  renderer->reset();
  for (auto& pair : source.bands) for (auto& b : pair) b.partials[0].frequency = 17003.2f;
  ratios = {1, .25, .5, std::exp2(7.0 / 12), 2, 4, 2, 4};
  for (std::size_t i = 0; i < 1000; ++i) {
    const auto y = renderer->process(source, ratios, i);
    for (const auto v : {3U, 4U, 5U, 6U, 7U})
      require(y[v][0] == 0 && y[v][1] == 0, "held carriers at/above Nyquist are not emitted");
  }
}

double frequency(const std::vector<float>& x) {
  std::size_t count = 0; double first = 0, last = 0;
  for (std::size_t i = 1; i < x.size(); ++i) if (x[i - 1] < 0 && x[i] >= 0) {
    const double at = i - 1 + x[i - 1] / (x[i - 1] - x[i]);
    if (!count) first = at;
    ++count; last = at;
  }
  require(count > 5, "enough held carrier cycles");
  return (count - 1) * 48000 / (last - first);
}

void glissAndVolume() {
  auto bank = std::make_unique<Bank>();
  bank->setAttack(0);
  std::vector<float> middle, settled, latched;
  for (std::size_t i = 0; i < 168000; ++i) {
    if (i == 24000) bank->setFreeze(Mode::FreezeGliss, .75f);
    if (i == 108000) bank->setFreeze(Mode::FreezeGliss, 1);
    const double hz = i < 48000 ? 196 : i < 120000 ? 293.6648 : 391.9954;
    const float x = .1 * std::sin(tau * hz * i / 48000);
    const auto y = bank->process({x, -.73f * x});
    if (i >= 69600 && i < 79200) middle.push_back(y[2][0]);
    if (i >= 100800 && i < 108000) settled.push_back(y[2][0]);
    if (i >= 132000) latched.push_back(y[2][0]);
  }
  const double mid = frequency(middle), end = frequency(settled), toe = frequency(latched);
  std::cout << "Gliss mid/settled/toe Hz=" << mid << '/' << end << '/' << toe
    << " targets=" << bank->ownership().freeze.targets() << '\n';
  require(mid > 100 && mid < 146, "gliss emits an intermediate moving carrier");
  const double moving = amplitude(middle, mid, 0, middle.size());
  const double endpoints = std::max(amplitude(middle, 98, 0, middle.size()), amplitude(middle, 146.8324, 0, middle.size()));
  require(moving > endpoints * 3, "gliss is a moving carrier rather than endpoint crossfade");
  require(std::fabs(1200 * std::log2(end / 146.8324)) < 3, "gliss reaches its new pitch");
  require(std::fabs(1200 * std::log2(toe / end)) < 3 && bank->ownership().freeze.latched(), "toe freezes current target pitch");
  require(bank->ownership().freeze.captures() == 1 && bank->ownership().freeze.targets() > 0, "target assignment retains capture identity");

  for (const bool dry : {false, true}) for (const float q : {.25f, .75f}) {
    bank->setFreeze(Mode::FreezeVolume, 0, dry); bank->reset();
    std::array<std::vector<float>, 2> held;
    for (std::size_t i = 0; i < 96000; ++i) {
      if (i == 24000) bank->setFreeze(Mode::FreezeVolume, q, dry);
      const float x = i < 48000 ? .1 * std::sin(tau * 196 * i / 48000) : 0;
      const auto y = bank->process({x, -.73f * x});
      if (i >= 72000) { held[0].push_back(y[0][0]); held[1].push_back(y[2][0]); }
    }
    const double a = amplitude(held[0], 196, 0, held[0].size());
    const double b = amplitude(held[1], 98, 0, held[1].size());
    std::cout << "Freeze volume dry/q=" << dry << '/' << q << " unison/down-one amplitude=" << a << '/' << b << '\n';
    require(std::fabs(a - (dry ? .1 * q : 0)) < 1e-4 && std::fabs(b - .1 * q) < 1e-4, "held Volume gain applies once and dry eligibility is respected");
  }
}

void freezeStationary() {
  auto bank = std::make_unique<Bank>();
  bank->setAttack(0);
  std::array<std::vector<float>, 8> output;
  for (auto& x : output) x.resize(96000);
  double anti = 0;
  for (std::size_t i = 0; i < 96000; ++i) {
    if (i == 24000) bank->setFreeze(Mode::FreezeVolume, 1);
    const float x = .1 * std::sin(tau * (i < 48000 ? 196 : 493.883) * i / 48000);
    const auto y = bank->process({x, -.73f * x});
    for (std::size_t v = 0; v < 8; ++v) {
      output[v][i] = y[v][0];
      if (i >= 60000) anti = std::max(anti, std::fabs(y[v][1] + .73 * y[v][0]));
    }
  }
  const auto& freeze = bank->ownership().freeze;
  require(freeze.captures() == 1 && freeze.targets() == 0 && freeze.latched(), "toe captures once and never retargets");
  require(anti < 2e-5, "held unequal anti-phase stereo");
  constexpr std::array<double, 8> semitones{0, -24, -12, 7, 12, 24, 12, 24};
  for (std::size_t v = 0; v < 8; ++v) {
    const double ratio = std::exp2(semitones[v] / 12);
    const double held = amplitude(output[v], 196 * ratio, 72000, 24000);
    const double replaced = amplitude(output[v], 493.883 * ratio, 72000, 24000);
    std::cout << "Freeze voice=" << v << " held gain/replacement dB=" << db(held, .1) << '/' << db(replaced, .1) << '\n';
    require(std::fabs(db(held, .1)) < 1 && db(replaced, .1) < -45, "held capture survives replacement input");
  }
  bank->setFreeze(Mode::FreezeVolume, 0);
  for (std::size_t i = 0; i < 20000; ++i) bank->process({});
  require(bank->ownership().freeze.state() == ardor::pog3::SpectralFreeze::State::Live, "heel releases hold");
  bank->reset(); bank->setFreeze(Mode::FreezeVolume, 1);
  for (std::size_t i = 0; i < 10000; ++i) require(bank->process({}) == Bank::Frame{}, "initial toe silence remains silent");
  require(bank->ownership().freeze.captures() == 0, "initial silence does not acquire identities");
}

auto partitioned(const std::vector<std::size_t>& partitions) {
  auto bank = std::make_unique<Bank>(); bank->setAttack(.1f);
  std::vector<Bank::Frame> out(24000);
  std::size_t i = 0, block = 0;
  while (i < out.size()) {
    const auto end = std::min(out.size(), i + partitions[block++ % partitions.size()]);
    for (; i < end; ++i) {
      if (i % 48 == 0) bank->transpose(.25 + .75 * (i / 48 % 17) / 16);
      if (i == 12000) bank->setFreeze(Mode::FreezeGliss, .75f);
      const float x = .1 * std::sin(tau * 196 * i / 48000);
      out[i] = bank->process({x, -.73f * x});
    }
  }
  require(bank->ownership().freeze.captures() == 1, "partition workload exercises held output");
  return out;
}
}
int main(int argc, char** argv) {
  try {
    require(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--short-hold"), "usage: erb-attack-freeze-quality [--short-hold]");
    attackOff(); heldNumerical(argc == 2 ? 4 : 120); heldCaptureCenters(); heldMovingAndAlias(); attackNotes(196, 493.883, true);
    attackNotes(82.4069, 87.3071, false); freezeStationary(); glissAndVolume();
    require(partitioned({64}) == partitioned({1, 17, 63, 128, 5}), "Attack/freeze partition invariance with absolute controls");
    std::cout << "Attack/freeze bank fixed object bytes=" << sizeof(Bank) << '\n';
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
