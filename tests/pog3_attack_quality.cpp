#include "daisyfx/pog3/PolyphonicPitchBank.h"
#include "pog3_artifacts.h"

#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace ardor::pog3;
constexpr double pi = 3.14159265358979323846;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
double level(const std::vector<float>& signal, double frequency, std::size_t start, std::size_t length) {
  std::complex<double> sum{};
  double weights = 0;
  for (std::size_t i = 0; i < length; ++i) {
    const double w = .5 - .5 * std::cos(2 * pi * i / length);
    const double phase = -2 * pi * frequency * (start + i) / kSampleRate;
    sum += w * static_cast<double>(signal[start + i]) * std::complex<double>{std::cos(phase), std::sin(phase)};
    weights += w;
  }
  return 2 * std::abs(sum) / weights;
}
double db(double a, double b) { return 20 * std::log10(std::max(a / std::max(b, 1e-20), 1e-20)); }

// At -24 the held/new fundamentals are less than two cycles apart in the
// mandatory 50 ms observation. A plain projection leaks held A into B. Fit
// both fundamentals and the shared harmonic together rather than relaxing the
// gate or calling that projection leakage an attack defect.
double separatedLevel(const std::vector<float>& signal, double target, double other, double shared,
                      std::size_t start, std::size_t length) {
  constexpr std::size_t columns = 7;
  std::array<std::array<double, columns + 1>, columns> matrix{};
  for (std::size_t i = 0; i < length; ++i) {
    const double w = .5 - .5 * std::cos(2 * pi * i / length);
    std::array<double, columns> basis{1};
    const std::array<double, 3> frequencies{target, other, shared};
    for (std::size_t f = 0; f < frequencies.size(); ++f) {
      const double phase = 2 * pi * frequencies[f] * (start + i) / kSampleRate;
      basis[1 + 2 * f] = std::cos(phase); basis[2 + 2 * f] = std::sin(phase);
    }
    for (std::size_t r = 0; r < columns; ++r) {
      for (std::size_t c = 0; c < columns; ++c) matrix[r][c] += w * basis[r] * basis[c];
      matrix[r][columns] += w * basis[r] * signal[start + i];
    }
  }
  for (std::size_t p = 0; p < columns; ++p) {
    std::size_t pivot = p;
    for (std::size_t r = p + 1; r < columns; ++r) if (std::fabs(matrix[r][p]) > std::fabs(matrix[pivot][p])) pivot = r;
    std::swap(matrix[p], matrix[pivot]);
    require(std::fabs(matrix[p][p]) > 1e-8, "partial measurement is not singular");
    const double divisor = matrix[p][p];
    for (std::size_t c = p; c <= columns; ++c) matrix[p][c] /= divisor;
    for (std::size_t r = 0; r < columns; ++r) if (r != p) {
      const double factor = matrix[r][p];
      for (std::size_t c = p; c <= columns; ++c) matrix[r][c] -= factor * matrix[p][c];
    }
  }
  return std::hypot(matrix[1][columns], matrix[2][columns]);
}

void independence(bool focus, bool sharedHarmonics, double fA = 196, double fB = 329.6276) {
  PolyphonicPitchBank reference, swell;
  reference.setFocus(focus); swell.setFocus(focus);
  require(swell.setAttackSeconds(.5f), "500 ms attack accepted");
  reference.prepare(); swell.prepare();
  constexpr std::size_t samples = 3 * 48000;
  std::array<std::vector<float>, 6> a, b;
  for (auto& signal : a) signal.resize(samples);
  for (auto& signal : b) signal.resize(samples);
  for (std::size_t i = 0; i < samples; ++i) {
    double x = .1 * std::sin(2 * pi * fA * i / kSampleRate);
    if (sharedHarmonics) x += .04 * std::sin(2 * pi * 3 * fA * i / kSampleRate);
    if (i >= 48000) {
      const double frequency = sharedHarmonics ? 1.5 * fA : fB;
      x += .08 * std::sin(2 * pi * frequency * i / kSampleRate);
      if (sharedHarmonics) x += .04 * std::sin(2 * pi * 2 * frequency * i / kSampleRate);
    }
    const auto raw = reference.process({static_cast<float>(x), static_cast<float>(-x)});
    const auto out = swell.process({static_cast<float>(x), static_cast<float>(-x)});
    for (std::size_t v = 0; v < 6; ++v) {
      a[v][i] = raw[v].left; b[v][i] = out[v].left;
      require(std::isfinite(out[v].left) && std::fabs(out[v].right + out[v].left) < 1e-5f,
              "anti-phase input shares attack without cancellation");
    }
  }
  for (std::size_t v = 0; v < 6; ++v) {
    const double ratio = std::exp2(kVoiceSemitones[v] / 12.0);
    const std::size_t delay = v >= 4 && !focus ? PolyphonicPitchBank::kShortDelay : PolyphonicPitchBank::kLongDelay;
    const std::size_t onset = 48000 + delay;
    const double f = (sharedHarmonics ? 1.5 * fA : fB) * ratio;
    const auto measure = [&](const std::vector<float>& signal, double target, double other, std::size_t start, std::size_t length) {
      return separatedLevel(signal, target, other, 3 * fA * ratio, start, length);
    };
    const double held = db(measure(b[v], fA * ratio, f, onset, 4800), measure(a[v], fA * ratio, f, onset, 4800));
    const double early = db(measure(b[v], f, fA * ratio, onset, 2400), measure(a[v], f, fA * ratio, onset, 2400));
    const double late = db(measure(b[v], f, fA * ratio, onset + 26400, 9600), measure(a[v], f, fA * ratio, onset + 26400, 9600));
    std::cout << "Attack independence Focus=" << focus << " shared=" << sharedHarmonics << " input=" << fA << " voice=" << v
              << " held=" << held << " dB new-first-50ms=" << early << " dB settled=" << late << " dB\n";
    require(std::fabs(held) < 1, "new note must not duck held note by 1 dB");
    require(early < -10, "new note first 50 ms suppressed by at least 10 dB");
    require(std::fabs(late) < 1, "new note reaches established level within attack plus analysis tolerance");
  }
  require(reference.healthy() && swell.healthy() && swell.deadlineMisses() == 0, "attack retains staged deadlines");
}

void repeatedPluck() {
  PolyphonicPitchBank reference, swell;
  require(swell.setAttackSeconds(.5), "attack setting");
  reference.prepare(); swell.prepare();
  std::vector<float> a(3 * 48000), b(a.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    const float amplitude = i < 48000 ? .08f : .2f;
    const float x = amplitude * std::sin(2 * pi * 196 * i / kSampleRate);
    a[i] = reference.process({x, 0})[4].left;
    b[i] = swell.process({x, 0})[4].left;
  }
  const auto onset = 48000 + PolyphonicPitchBank::kShortDelay;
  const double early = level(b, 392, onset, 2400);
  const double prior = level(b, 392, onset - 9600, 4800);
  const double full = level(a, 392, onset, 2400);
  const double late = level(b, 392, onset + 26400, 9600);
  std::cout << "Repeated pluck sustain=" << prior << " early=" << early << " reference=" << full << " settled=" << late << '\n';
  require(early > prior * .8913 && early - prior < .5 * (full - prior),
          "repeat retains old sustain within 1 dB and suppresses the positive excitation");
  require(std::fabs(db(late, .2)) < 1, "repeat reaches full level");
}

void contracts() {
  PolyphonicPitchBank bank;
  require(!bank.setAttackSeconds(std::numeric_limits<float>::quiet_NaN()), "nonfinite Attack rejected");
  require(bank.setAttackSeconds(4) && bank.attackSeconds() == 3, "finite Attack clamps to maximum");
  require(bank.setAttackSeconds(-1) && bank.attackSeconds() == 0, "finite Attack clamps to exact off");
  bank.setFocus(true); bank.setWarp(0); bank.prepare();
  DryAttackRouter router;
  for (std::size_t i = 0; i < 10000; ++i) {
    const PitchStereo input{.2f * static_cast<float>(std::sin(i * .3)), -.13f * static_cast<float>(std::cos(i * .21))};
    const auto processed = bank.process(input)[0];
    const auto dry = router.process(input, processed, false);
    require(dry.left == input.left && dry.right == input.right, "Dry Attack off is exact immediate stereo identity");
  }
  router.reset();
  for (std::size_t i = 0; i < 3000; ++i) {
    const bool enabled = i < 300 || (i >= 500 && i < 1800);
    const auto output = router.process({.3, -.3}, {.1, -.1}, enabled);
    require(std::fabs(output.left - (.3f * (1 - router.position()) + .1f * router.position())) < 1e-7,
            "dry route reversals retain a continuous bounded fade");
  }
  require(router.position() == 0, "latest dry-off target settles exactly");
  require(bank.setAttackSeconds(.5f), "retained attack setting");
  bank.reset(); bank.reset();
  for (std::size_t i = 0; i < 10000; ++i) {
    const auto output = bank.process({0, 0});
    for (const auto voice : output) require(voice.left == 0 && voice.right == 0, "reset clears attack and spectral histories");
  }
  require(bank.attackSeconds() == .5f, "reset retains attack target");
}

void arpeggio(bool focus) {
  PolyphonicPitchBank reference, swell;
  reference.setFocus(focus); swell.setFocus(focus); swell.setAttackSeconds(.3f);
  reference.prepare(); swell.prepare();
  constexpr std::array<double, 3> frequencies{196, 329.6276, 493.8833};
  std::array<std::vector<float>, 6> a, b;
  for (auto& signal : a) signal.resize(3 * 48000);
  for (auto& signal : b) signal.resize(3 * 48000);
  for (std::size_t i = 0; i < a[0].size(); ++i) {
    double x = 0;
    for (std::size_t f = 0; f < frequencies.size(); ++f)
      if (i >= f * 36000) x += .06 * std::sin(2 * pi * frequencies[f] * i / kSampleRate);
    const auto raw = reference.process({static_cast<float>(x), 0}), out = swell.process({static_cast<float>(x), 0});
    for (std::size_t v = 0; v < 6; ++v) { a[v][i] = raw[v].left; b[v][i] = out[v].left; }
  }
  for (std::size_t v = 0; v < 6; ++v) {
    const double ratio = std::exp2(kVoiceSemitones[v] / 12.0);
    const auto onset = 72000 + (v >= 4 && !focus ? PolyphonicPitchBank::kShortDelay : PolyphonicPitchBank::kLongDelay);
    for (std::size_t held = 0; held < 2; ++held) {
      const double delta = db(separatedLevel(b[v], frequencies[held] * ratio, frequencies[1 - held] * ratio, frequencies[2] * ratio, onset, 4800),
                              separatedLevel(a[v], frequencies[held] * ratio, frequencies[1 - held] * ratio, frequencies[2] * ratio, onset, 4800));
      require(std::fabs(delta) < 1, "third arpeggio note preserves both held notes");
    }
    const double early = db(separatedLevel(b[v], frequencies[2] * ratio, frequencies[0] * ratio, frequencies[1] * ratio, onset, 2400),
                            separatedLevel(a[v], frequencies[2] * ratio, frequencies[0] * ratio, frequencies[1] * ratio, onset, 2400));
    const double late = db(level(b[v], frequencies[2] * ratio, onset + 16800, 9600), level(a[v], frequencies[2] * ratio, onset + 16800, 9600));
    std::cout << "Arpeggio Focus=" << focus << " voice=" << v << " early=" << early << " dB settled=" << late << " dB\n";
    require(early < -10 && std::fabs(late) < 1, "third arpeggio note independently swells and settles");
  }
}

void bendAndFocus() {
  PolyphonicPitchBank reference, swell;
  swell.setAttackSeconds(.2f);
  reference.prepare(); swell.prepare();
  double phase = 0, rawEnergy = 0, outEnergy = 0;
  double worst = 0;
  for (std::size_t i = 0; i < 4 * 48000; ++i) {
    if (i == 72000 || i == 108000) { reference.setFocus(true); swell.setFocus(true); }
    if (i == 80000 || i == 130000) { reference.setFocus(false); swell.setFocus(false); }
    const double bend = std::clamp((static_cast<double>(i) - 48000) / 96000, 0.0, 1.0);
    phase += 2 * pi * 196 * std::exp2(bend * 3 / 12) / kSampleRate;
    const float x = .1 * std::sin(phase);
    const auto a = reference.process({x, -.4f * x}), b = swell.process({x, -.4f * x});
    if (i >= 48000) {
      rawEnergy += a[4].left * a[4].left; outEnergy += b[4].left * b[4].left;
      if (i % 4800 == 4799) {
        const double delta = 10 * std::log10(outEnergy / rawEnergy);
        worst = std::max(worst, std::fabs(delta));
        require(std::fabs(delta) < 1, "a continuous bend/Focus reversal must not restart the established swell");
        rawEnergy = outEnergy = 0;
      }
    }
  }
  std::cout << "Bend/Focus worst level change=" << worst << " dB\n";
}

void activationAndPartitions() {
  PolyphonicPitchBank reference, switched;
  reference.prepare(); switched.prepare();
  double worst = 0;
  for (std::size_t i = 0; i < 2 * 48000; ++i) {
    if (i == 48000) switched.setAttackSeconds(.5f);
    const float x = .1 * std::sin(2 * pi * 196 * i / kSampleRate);
    const auto a = reference.process({x, -.3f * x}), b = switched.process({x, -.3f * x});
    for (std::size_t v = 1; v < 6; ++v) worst = std::max(worst, static_cast<double>(std::fabs(a[v].left - b[v].left)));
  }
  require(worst < 1e-6, "enabling Attack over existing sustain does not restart generated voices");
  const auto render = [](const std::vector<std::size_t>& chunks) {
    PolyphonicPitchBank bank;
    bank.setAttackSeconds(.3f); bank.prepare();
    std::vector<PitchVoices> out(12013);
    std::size_t pos = 0, chunk = 0;
    while (pos < out.size()) {
      const auto end = std::min(out.size(), pos + chunks[chunk++ % chunks.size()]);
      for (; pos < end; ++pos) {
        if (pos == 4000) bank.setAttackSeconds(0);
        if (pos == 5000) bank.setAttackSeconds(.2f);
        if (pos % 480 == 0) { bank.setFocus((pos / 480) % 2); bank.setWarp((pos / 480) % 128 / 127.0f); }
        const float x = .1 * std::sin(2 * pi * 196 * pos / kSampleRate);
        out[pos] = bank.process({x, -.6f * x});
      }
    }
    require(bank.healthy() && bank.deadlineMisses() == 0, "attack jobs retain all logical deadlines");
    return out;
  };
  const auto a = render({12013}), b = render({1, 17, 48, 64, 128, 256});
  for (std::size_t i = 0; i < a.size(); ++i) for (std::size_t v = 0; v < 6; ++v)
    require(a[i][v].left == b[i][v].left && a[i][v].right == b[i][v].right,
            "Attack/Focus/Warp automation is exactly invariant to callback partitions");
  std::cout << "Activation held-voice maximum error=" << worst << "; attack callback partitions equal\n";
}

void capacityAndRecovery() {
  PolyphonicPitchBank bank;
  bank.setAttackSeconds(3); bank.prepare();
  std::uint32_t random = 0x504f4733;
  for (std::size_t i = 0; i < 36000; ++i) {
    PitchStereo input{};
    if (i < 12000) {
      const auto noise = [&] {
        random = 1664525 * random + 1013904223;
        return static_cast<float>((static_cast<double>(random) / 4294967296.0 - .5) * .4);
      };
      input = {noise(), noise()};
    }
    const auto output = bank.process(input);
    for (const auto voice : output) {
      require(std::isfinite(voice.left) && std::isfinite(voice.right), "capacity overflow remains finite");
      if (i > 24000) require(std::fabs(voice.left) < 1e-6 && std::fabs(voice.right) < 1e-6,
                             "capacity pressure drains to silence");
    }
    require(bank.attackFamilyCount() <= PolyphonicAttack::kFamilies, "harmonic family capacity remains bounded");
  }
  std::cout << "Capacity stress events=" << bank.attackCapacityEvents() << '\n';
  require(bank.attackCapacityEvents() > 0 && bank.healthy() && bank.deadlineMisses() == 0,
          "independent noise exercises the fixed-capacity fallback without missing a staged job");
  bank.reset();
  require(bank.attackCapacityEvents() == 0 && bank.attackFamilyCount() == 0, "reset clears capacity state");
}

void renders(const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);
  std::vector<PitchStereo> input(5 * 48000);
  for (std::size_t i = 0; i < input.size(); ++i) {
    double x = 0;
    constexpr std::array<double, 3> f{196, 329.6276, 493.8833};
    for (std::size_t note = 0; note < f.size(); ++note) {
      if (i < note * 48000 || i >= 4 * 48000) continue;
      const double age = (i - note * 48000) / kSampleRate;
      const double envelope = (1 - std::exp(-age * 1000)) * std::exp(-age * .15);
      x += envelope * (.05 * std::sin(2 * pi * f[note] * i / kSampleRate)
        + .018 * std::sin(2 * pi * 2 * f[note] * i / kSampleRate)
        + .008 * std::sin(2 * pi * 3 * f[note] * i / kSampleRate));
    }
    input[i] = {static_cast<float>(x), static_cast<float>(.8 * x)};
  }
  pog3_test::writeRender(directory / "attack-input-synthetic-arpeggio.wav", input);
  for (const bool focus : {false, true}) for (const int route : {0, 1, 2}) {
    PolyphonicPitchBank bank;
    bank.setFocus(focus); bank.setAttackSeconds(route ? .5f : 0); bank.prepare();
    DryAttackRouter dry;
    dry.reset(route == 2);
    std::vector<PitchStereo> output(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
      const auto voices = bank.process(input[i]);
      const auto direct = dry.process(input[i], voices[0], route == 2);
      output[i] = {.25f * direct.left, .25f * direct.right};
      for (std::size_t v = 1; v < voices.size(); ++v) {
        output[i].left += .25f * voices[v].left;
        output[i].right += .25f * voices[v].right;
      }
    }
    require(bank.healthy(), "attack render remains healthy");
    const std::string name = std::string(route == 0 ? "attack-reference" : route == 1 ? "attack-raw-dry" : "attack-processed-dry")
      + (focus ? "-focus-on.wav" : "-focus-off.wav");
    pog3_test::writeRender(directory / name, output);
  }
}
}

int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string_view(argv[1]) == "--render") { renders(argv[2]); return 0; }
    if (argc == 2 && std::string_view(argv[1]) == "--capacity") { capacityAndRecovery(); return 0; }
    require(argc == 1, "usage: pedal-pog3-attack-quality [--render directory|--capacity]");
    contracts();
    for (const bool focus : {false, true}) for (const bool shared : {false, true}) independence(focus, shared);
    independence(false, false, 82.4069, 130.8128); independence(true, false, 82.4069, 130.8128);
    repeatedPluck();
    arpeggio(false); arpeggio(true); bendAndFocus();
    activationAndPartitions();
    capacityAndRecovery();
    std::cout << "Polyphonic attack independence and dry-routing gates passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
