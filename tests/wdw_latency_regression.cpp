#include "audio/WdwRoutingBuilder.h"
#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/hosted/dsp/halfband_resampler.h"
#include "dsp/NamProcessor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace ardor;
constexpr float kThreshold = 1.0e-7f;
constexpr std::size_t kProbeFrames = 4096;
using Samples = std::vector<StereoSample>;

void require(bool ok, const std::string& message)
{
  if (!ok) throw std::runtime_error(message);
}

struct Fixture {
  std::string name;
  std::size_t latency;
  nlohmann::json params;
  std::function<void(RuntimeChain&)> add;
};

template<class Processor>
Fixture effect(std::string name, std::size_t latency, nlohmann::json params)
{
  return {std::move(name), latency, params, [params](RuntimeChain& chain) {
    Processor p;
    std::string error;
    require(p.configure(params, 48000.0f, error), error);
#ifdef ARDOR_WDW_HAS_CONSOLE_EQ
    if constexpr (std::is_same_v<Processor, ConsoleEqProcessor>)
      chain.addConsoleEq("effect", std::move(p));
    else
#endif
      chain.addDistortion("effect", std::move(p));
  }};
}

std::vector<Fixture> fixtures()
{
  std::vector<Fixture> result{
    effect<RatProcessor>("rat", 26, {{"mode", "rat"}}),
    effect<CheeseProcessor>("big_cheese", 26, {{"mode", "big_cheese"}}),
    effect<TapeProcessor>("tape", 76, {{"mode", "tape"}}),
#ifdef ARDOR_WDW_HAS_CONSOLE_EQ
    effect<ConsoleEqProcessor>("console_eq", 15, defaultConsoleEqParams()),
#endif
  };
  result.push_back({"wah", 23, {{"mode", "gcb95"}}, [](RuntimeChain& chain) {
    WahProcessor p;
    std::string error;
    require(p.configure({{"mode", "gcb95"}}, 48000.0f,
      std::filesystem::path(ARDOR_SOURCE_DIR) / "assets/wah/gcb95.wahtable", error), error);
    chain.addWah("effect", std::move(p));
  }});
  // Cover the entire shipped Daisy catalog, including every reverb adapter,
  // native-rate plate, modulation dry copies and intentional effect delays.
  for (const auto& descriptor : daisyFxCatalog()) {
    const auto params = defaultDaisyFxParams(descriptor);
    const auto type = descriptor.blockType;
    const auto latency = type == "reverb" ? (descriptor.mode == "plate" ? 0U : 31U)
      : descriptor.mode == "phaser_ph2" ? 15U : 0U;
    result.push_back({type + "/" + descriptor.mode, latency, params,
      [params, type, latency](RuntimeChain& chain) {
        DaisyFxProcessor p;
        std::string error;
        require(p.configure(type, params, 48000.0f, error), error);
        require(p.latencyFrames() == latency, "unexpected Daisy declared latency");
        chain.addDaisy("effect", std::move(p));
      }});
  }
  result.push_back({"nam/lstm", 0, {}, [](RuntimeChain& chain) {
    require(chain.addNam(ARDOR_NAM_EXAMPLE_MODEL, 48000.0, 256, "effect"), "load NAM");
  }});
  result.push_back({"cab/leading_silence", 0, {}, [](RuntimeChain& chain) {
    chain.addCab({0, 0, 0, 1, .25f, -.1f}, 1, 1, "effect");
  }});
  result.push_back({"irreverb/500ms", 0, {{"mix", 1}, {"preDelayMs", 500}}, [](RuntimeChain& chain) {
    std::string error;
    require(chain.addIrReverb("effect", {1, .25f}, {}, 48000, error), error);
    require(chain.setIrReverbParameter("effect", "mix", 1), "IR mix");
    require(chain.setIrReverbParameter("effect", "preDelayMs", 500), "IR pre-delay");
  }});
  result.push_back({"dynamics/compressor", 0, {}, [](RuntimeChain& chain) {
    CompressorProcessor p;
    std::string error;
    require(p.configure(nlohmann::json::object(), 48000, error), error);
    chain.addCompressor("effect", std::move(p));
  }});
  result.push_back({"dynamics/gate", 0, {}, [](RuntimeChain& chain) {
    NoiseGateProcessor p;
    std::string error;
    require(p.configure(nlohmann::json::object(), 48000, error), error);
    chain.addNoiseGate("effect", std::move(p));
  }});
  result.push_back({"dynamics/transient", 0, {}, [](RuntimeChain& chain) {
    TransientShaperProcessor p;
    std::string error;
    require(p.configure(nlohmann::json::object(), 48000, error), error);
    chain.addTransientShaper("effect", std::move(p));
  }});
  result.push_back({"eq/parametric", 0, {}, [](RuntimeChain& chain) {
    std::string error;
    require(chain.addParametricEq("effect", {}, 48000, error), error);
  }});
  result.push_back({"stereo/widener", 0, {}, [](RuntimeChain& chain) {
    std::string error;
    require(chain.addStereoWidener("effect", 48000, error), error);
  }});
  return result;
}

Samples render(RuntimeChain& chain, std::size_t quantum, float amplitude = 1,
               bool scalar = false, std::size_t frames = kProbeFrames)
{
  chain.prepareBlockSize(quantum);
  chain.reset();
  const auto roundedFrames = ((frames + quantum - 1) / quantum) * quantum;
  Samples result(roundedFrames);
  std::vector<float> input(quantum), left(quantum), right(quantum);
  for (std::size_t offset = 0; offset < roundedFrames; offset += quantum) {
    std::fill(input.begin(), input.end(), 0);
    if (!offset) input[0] = amplitude;
    if (scalar) {
      for (std::size_t i = 0; i < quantum; ++i)
        result[offset + i] = chain.process({input[i], input[i]});
    } else {
      chain.processBlock(input.data(), left.data(), right.data(), quantum);
      for (std::size_t i = 0; i < quantum; ++i) result[offset + i] = {left[i], right[i]};
    }
  }
  require(chain.nonFiniteBlockCount() == 0, "non-finite probe output");
  return result;
}

struct Measurement {
  std::optional<std::size_t> onset;
  std::size_t peakFrame = 0;
  float peak = 0;
};
Measurement measure(const Samples& samples, float threshold = kThreshold)
{
  Measurement m;
  for (std::size_t i = 0; i < samples.size(); ++i) {
    require(std::isfinite(samples[i].left) && std::isfinite(samples[i].right), "non-finite sample");
    const auto magnitude = std::max(std::abs(samples[i].left), std::abs(samples[i].right));
    if (!m.onset && magnitude >= threshold) m.onset = i;
    if (magnitude > m.peak) { m.peak = magnitude; m.peakFrame = i; }
  }
  return m;
}

void measurements(const std::vector<Fixture>& all)
{
  std::cout << "block,declared_frames,onset_frames,peak_frame,peak_level,bypass_onset,params\n";
  for (const auto& fixture : all) {
    RuntimeChain chain;
    fixture.add(chain);
    const auto m = measure(render(chain, 64));
    require(chain.setBlockEnabled("effect", false), "disable " + fixture.name);
    const auto bypass = measure(render(chain, 64));
    std::cout << fixture.name << ',' << fixture.latency << ',';
    if (m.onset) std::cout << *m.onset;
    std::cout << ',' << m.peakFrame << ',' << std::setprecision(9) << m.peak << ',';
    if (bypass.onset) std::cout << *bypass.onset;
    // CSV-escape the documented configuration.
    std::string params = fixture.params.dump();
    std::cout << ",\"";
    for (char c : params) { if (c == '"') std::cout << '"'; std::cout << c; }
    std::cout << "\"\n";
  }
}

void bypassContracts(const std::vector<Fixture>& all)
{
  for (const auto& fixture : all) {
    for (const auto quantum : {1U, 16U, 64U, 128U, 256U}) {
      RuntimeChain chain;
      fixture.add(chain);
      require(chain.latencyFrames() == fixture.latency, fixture.name + " declared chain latency");
      require(chain.setBlockEnabled("effect", false), "disable " + fixture.name);
      require(chain.latencyFrames() == fixture.latency, fixture.name + " bypass changed metadata");
      const auto output = render(chain, quantum, .5f, false, 512);
      for (std::size_t i = 0; i < output.size(); ++i) {
        const float expected = i == fixture.latency ? .5f : 0;
        require(output[i].left == expected && output[i].right == expected,
          fixture.name + " bypass must preserve declared delay at quantum " + std::to_string(quantum)
            + ", frame " + std::to_string(i));
      }
      const auto scalar = render(chain, quantum, .5f, true, 512);
      for (std::size_t i = 0; i < output.size(); ++i)
        require(output[i].left == scalar[i].left && output[i].right == scalar[i].right,
          fixture.name + " scalar/block bypass mismatch");
      // reset must clear all delay history, not leak the preceding impulse.
      chain.reset();
      for (std::size_t i = 0; i < 512; ++i) {
        const auto y = chain.process({0, 0});
        require(y.left == 0 && y.right == 0, fixture.name + " bypass reset leaked history");
      }
      // Scalar API accepts stereo input: bypass must retain both independent
      // channels even for processors whose active circuit is mono.
      Samples stereo(512);
      for (std::size_t i = 0; i < stereo.size(); ++i) {
        const StereoSample x{.3f * std::sin(i * .13f), -.2f * std::cos(i * .19f)};
        stereo[i] = x;
        const auto y = chain.process(x);
        const auto expected = i >= fixture.latency ? stereo[i - fixture.latency] : StereoSample{};
        require(y.left == expected.left && y.right == expected.right,
          fixture.name + " stereo bypass delay");
      }
      require(chain.setBlockEnabled("effect", true), "reenable " + fixture.name);
      require(chain.latencyFrames() == fixture.latency, fixture.name + " reenable changed metadata");
      chain.clear();
      require(chain.latencyFrames() == 0, fixture.name + " clear left stale metadata");
    }
  }
}

const Fixture& findFixture(const std::vector<Fixture>& all, const std::string& name)
{
  const auto it = std::find_if(all.begin(), all.end(), [&](const auto& f) { return f.name == name; });
  require(it != all.end(), "missing fixture " + name);
  return *it;
}

void impulseContracts(const std::vector<Fixture>& all)
{
  const std::vector<std::tuple<const char*, int, int>> baselines{
    {"rat", 9, 39}, {"tape", 59, 75}, {"mod/phaser_ph2", 0, 15},
#ifdef ARDOR_WDW_HAS_CONSOLE_EQ
    {"console_eq", 0, 15},
#endif
  };
  for (const auto& [name, onset, peak] : baselines) {
    const auto& fixture = findFixture(all, name);
    for (const auto quantum : {1U, 16U, 64U, 128U, 256U}) {
      RuntimeChain chain;
      fixture.add(chain);
      const auto m = measure(render(chain, quantum));
      require(m.onset == onset && m.peakFrame == peak, std::string(name) + " impulse baseline changed");
      require(*m.onset < fixture.latency, std::string(name) + " probe must expose pre-ringing");
      require(chain.latencyFrames() == fixture.latency, std::string(name) + " metadata depends on quantum");
    }
  }
  // Unlike bypass, the processor impulse peak is shaped by its circuit.
  require(39 != findFixture(all, "rat").latency, "RAT peak is not fixed latency");
  for (const auto& fixture : all) {
    RuntimeChain chain;
    fixture.add(chain);
    const auto block = render(chain, 64, .1f);
    RuntimeChain scalarChain;
    fixture.add(scalarChain);
    const auto scalar = render(scalarChain, 64, .1f, true);
    for (std::size_t i = 0; i < block.size(); ++i) {
      require(std::abs(block[i].left - scalar[i].left) < 1e-4f * (1 + std::abs(block[i].left))
           && std::abs(block[i].right - scalar[i].right) < 1e-4f * (1 + std::abs(block[i].right)),
        fixture.name + " scalar/block active impulse mismatch at " + std::to_string(i));
    }
  }
}

void bypassTransitions(const std::vector<Fixture>& all)
{
  for (const auto& fixture : all) {
    if (!fixture.latency) continue;
    for (bool blocks : {false, true}) {
      RuntimeChain active, fading;
      fixture.add(active); fixture.add(fading);
      active.prepareBlockSize(64); fading.prepareBlockSize(64);
      active.reset(); fading.reset();
      std::array<float, 64> input{}, aL{}, aR{}, bL{}, bR{};
      std::vector<float> history(1536);
      float mix = 1;
      for (std::size_t offset = 0; offset < history.size(); offset += 64) {
        if (offset == 1024) require(fading.setBlockEnabled("effect", false), "start bypass fade");
        for (std::size_t i = 0; i < input.size(); ++i)
          history[offset + i] = input[i] = .01f * std::sin((offset + i) * .17f);
        if (blocks) {
          active.processBlock(input.data(), aL.data(), aR.data(), 64);
          fading.processBlock(input.data(), bL.data(), bR.data(), 64);
        } else {
          for (std::size_t i = 0; i < 64; ++i) {
            const auto a = active.process({input[i], input[i]});
            const auto b = fading.process({input[i], input[i]});
            aL[i] = a.left; aR[i] = a.right; bL[i] = b.left; bR[i] = b.right;
          }
        }
        for (std::size_t i = 0; i < 64; ++i) {
          const auto frame = offset + i;
          if (frame >= 1024) mix = std::max(0.0f, mix - 1.0f / 480.0f);
          const float dry = frame >= fixture.latency ? history[frame - fixture.latency] : 0;
          const float expectedL = dry + mix * (aL[i] - dry);
          const float expectedR = dry + mix * (aR[i] - dry);
          require(std::abs(bL[i] - expectedL) < 2e-5f * (1 + std::abs(expectedL))
               && std::abs(bR[i] - expectedR) < 2e-5f * (1 + std::abs(expectedR)),
            fixture.name + " fade must blend latency-aligned dry at frame " + std::to_string(frame));
        }
      }
    }
  }
}

void mixEndpoints(const std::vector<Fixture>& all)
{
  for (const auto& fixture : all) {
    if (!fixture.params.contains("mix")) continue;
    for (float mix : {0.0f, .25f, .5f, 1.0f}) {
      RuntimeChain chain;
      fixture.add(chain);
      bool changed = chain.setDaisyParameter("effect", "mix", mix)
#ifdef ARDOR_WDW_HAS_CONSOLE_EQ
        || chain.setConsoleEqParameter("effect", "mix", mix)
#endif
        || chain.setDistortionParameter("effect", "mix", mix)
        || chain.setIrReverbParameter("effect", "mix", mix);
      require(changed, "set mix " + fixture.name);
      chain.reset();
      // Live Daisy targets enter at the next control tick. Consume that tick
      // before resetting for a stationary endpoint measurement.
      (void)chain.process({});
      require(chain.latencyFrames() == fixture.latency, fixture.name + " mix changed fixed latency");
      const auto samples = render(chain, 64, .1f);
      // The zero-mix endpoint must be the exact delayed input. Daisy defaults
      // use unity level. Other endpoints may include intentional effect timing.
      if (mix == 0) {
        for (std::size_t i = 0; i < samples.size(); ++i) {
          const float expected = i == fixture.latency ? .1f : 0;
          // Some modulation modes use an approximate cosine for dry gain.
          const float tolerance = fixture.name.starts_with("mod/") && expected != 0 ? 3e-5f : 1e-6f;
          require(std::abs(samples[i].left - expected) < tolerance
               && std::abs(samples[i].right - expected) < tolerance,
            fixture.name + " dry mix endpoint is not latency aligned frame=" + std::to_string(i) + " left=" + std::to_string(samples[i].left) + " expected=" + std::to_string(expected));
        }
      }
    }
  }
  RuntimeChain chain;
  std::vector<std::string> names{"rat", "big_cheese", "tape", "wah", "mod/phaser_ph2", "reverb/cloud"};
  std::size_t expectedLatency = 197;
#ifdef ARDOR_WDW_HAS_CONSOLE_EQ
  names.push_back("console_eq");
  expectedLatency += 15;
#endif
  for (const auto& name : names) findFixture(all, name).add(chain);
  require(chain.latencyFrames() == expectedLatency, "serial chain must sum all declared delays");
  // IDs are reused only in this metadata/move test. The builder's serial
  // audio tests below use unique IDs for independently bypassed processors.
  RuntimeChain moved = std::move(chain);
  require(moved.latencyFrames() == expectedLatency, "move lost declared latency");
  moved.clear();
  require(moved.latencyFrames() == 0, "clear lost latency contract");
}

Samples roundTrip(unsigned stages, bool evenPhase = false)
{
  std::array<pedal::HalfbandInterpolator2x, 3> up;
  std::array<pedal::HalfbandDecimator2x, 3> down;
  if (evenPhase) {
    float ignored;
    for (unsigned stage = 0; stage < stages; ++stage) down[stage].Push(0, ignored);
  }
  Samples response(128);
  for (std::size_t frame = 0; frame < response.size(); ++frame) {
    std::vector<float> signal{frame == 0 ? 1.0f : 0.0f};
    for (unsigned stage = 0; stage < stages; ++stage) {
      std::vector<float> next;
      for (float x : signal) {
        const auto pair = up[stage].Process(x);
        next.insert(next.end(), pair.begin(), pair.end());
      }
      signal = std::move(next);
    }
    for (unsigned stage = stages; stage-- > 0;) {
      std::vector<float> next;
      for (float x : signal) {
        float y;
        if (down[stage].Push(x, y)) next.push_back(y);
      }
      signal = std::move(next);
    }
    require(signal.size() == 1, "resampler host-rate output count");
    response[frame] = {signal[0], signal[0]};
  }
  return response;
}

void halfbandContracts()
{
  for (unsigned stages : {1U, 2U, 3U}) {
    for (bool evenPhase : {false, true}) {
      const auto response = roundTrip(stages, evenPhase);
      double gain = 0, moment = 0;
      for (std::size_t i = 0; i < response.size(); ++i) {
        gain += response[i].left;
        moment += i * double(response[i].left);
      }
      const double ratio = unsigned{1} << stages;
      // Default decimation selects the last phase of each host frame. This
      // shifts the effective time origin by (ratio-1)/ratio host frames.
      const double groupDelay = 30 * (1 - 1 / ratio) - (evenPhase ? 0 : (ratio - 1) / ratio);
      require(std::abs(gain - 1) < .01, "halfband unity DC gain");
      require(std::abs(moment / gain - groupDelay) < .015, "halfband rate/phase group delay");
      const auto m = measure(response);
      require(m.onset && *m.onset < groupDelay, "halfband onset must precede centre");
      if (stages == 1 && evenPhase) require(m.peakFrame == 15, "PH-2/EQ even-phase centre");
      std::cerr << "halfband " << ratio << "x phase=" << (evenPhase ? "even" : "default")
                << " onset=" << *m.onset << " peak=" << m.peakFrame
                << " group_delay=" << moment / gain << '\n';
    }
  }
  // Hosted reverb adapter goes down first, then up, buffering the returned
  // pair exactly as DaisyFxProcessor does. Its neutral round-trip peak is 31.
  pedal::HalfbandDecimator2x down;
  pedal::HalfbandInterpolator2x up;
  std::array<float, 2> pair{};
  unsigned index = 0, count = 0;
  Samples response(128);
  for (std::size_t i = 0; i < response.size(); ++i) {
    float y;
    const float output = count ? pair[index++] : 0;
    if (count) --count;
    if (down.Push(i == 0 ? 1 : 0, y)) { pair = up.Process(y); index = 0; count = 2; }
    response[i] = {output, output};
  }
  const auto m = measure(response);
  require(m.peakFrame == 31, "reverb adapter peak must match declared 31-frame latency");
  require(m.onset && *m.onset < 31, "reverb adapter has pre-ringing");
  std::cerr << "reverb neutral adapter onset=" << *m.onset << " peak=" << m.peakFrame << '\n';
}

struct BuilderAssets {
  std::filesystem::path root = std::filesystem::temp_directory_path()
    / ("ardor-wdw-latency-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()));

  BuilderAssets()
  {
    std::filesystem::create_directories(root);
    std::filesystem::copy_file(ARDOR_NAM_EXAMPLE_MODEL, root / "model.nam");
    std::filesystem::copy_file(
      std::filesystem::path(ARDOR_SOURCE_DIR) / "assets/wah/gcb95.wahtable",
      root / "gcb95.wahtable");
  }
  ~BuilderAssets()
  {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};

const BuilderAssets& builderAssets()
{
  // Keep all file-backed builder fixtures under one confined root, including
  // when the NAM dependency cache or build directory is outside the checkout.
  static const BuilderAssets assets;
  return assets;
}

WdwRoutingBuildOptions buildOptions(std::size_t quantum = 64)
{
  WdwRoutingBuildOptions options;
  options.engine.blockSize = quantum;
  options.engine.assetRoot = builderAssets().root;
  options.program.executor.mode = WdwPairExecutionMode::Direct;
  options.program.executor.requireWorkerSetup = false;
  options.program.executor.requireRealtimeScheduling = false;
  options.program.executor.requireAffinity = false;
  options.program.mix = {std::sqrt(2.0f), 0, true, 1, 1, true};
  return options;
}

ChainPlan lane(const char* id)
{
  ChainPlan plan;
  ChainBlockPlan nam;
  nam.id = id; nam.type = "nam"; nam.status = ChainBlockStatus::Ready;
  nam.assetPath = builderAssets().root / "model.nam";
  // Real NAM instances are loaded; bypass isolates known linear alignment
  // from a model's nonlinear response and leaves topology validation intact.
  nam.enabled = false;
  plan.blocks.push_back(std::move(nam));
  return plan;
}
void append(ChainPlan& plan, std::string id, std::string type, nlohmann::json params,
            bool enabled = false, ChainBlockStatus status = ChainBlockStatus::Ready)
{
  ChainBlockPlan block;
  block.id = std::move(id); block.type = std::move(type); block.params = std::move(params);
  block.enabled = enabled; block.status = status;
  if (block.type == "wah") block.assetPath = builderAssets().root / "gcb95.wahtable";
  plan.blocks.push_back(std::move(block));
}

Samples renderProgram(WdwRoutingProgram& program, std::size_t frames = 512)
{
  const auto quantum = program.blockSize();
  const auto rounded = (frames + quantum - 1) / quantum * quantum;
  Samples result(rounded);
  std::vector<float> input(quantum), left(quantum), right(quantum);
  for (std::size_t offset = 0; offset < rounded; offset += quantum) {
    std::fill(input.begin(), input.end(), 0);
    if (!offset) input[0] = 1;
    WdwRoutingProcessResult processed;
    require(program.processBlock(input.data(), left.data(), right.data(), quantum, processed), "render WDW");
    require(processed.pairReady && processed.outputReady, "direct WDW pair not ready");
    for (std::size_t i = 0; i < quantum; ++i) result[offset + i] = {left[i], right[i]};
  }
  return result;
}

void builderContracts()
{
  for (unsigned scenario = 0; scenario < 6; ++scenario) {
    for (const auto quantum : {1U, 16U, 64U, 128U, 256U}) {
      auto dry = lane("dry-nam"), wet = lane("wet-nam");
      std::size_t d = 0, w = 0;
      if (scenario == 0) { append(dry, "rat", "distortion", {{"mode", "rat"}}); d = 26; }
      if (scenario == 1) {
        append(dry, "rat", "distortion", {{"mode", "rat"}}); d = 26;
        append(wet, "cloud", "reverb", {{"mode", "cloud"}}); w = 31;
      }
      if (scenario == 2) {
#ifdef ARDOR_WDW_HAS_CONSOLE_EQ
        append(dry, "eq", "eq", {{"mode", "console_1073"}}); d = 15;
        append(wet, "ph2", "mod", {{"mode", "phaser_ph2"}}); w = 15;
#else
        append(dry, "wah-1", "wah", {{"mode", "gcb95"}});
        append(dry, "wah-2", "wah", {{"mode", "gcb95"}}); d = 46;
        append(wet, "ph2", "mod", {{"mode", "phaser_ph2"}});
        append(wet, "cloud", "reverb", {{"mode", "cloud"}}); w = 46;
#endif
      }
      if (scenario == 3) {
        append(dry, "rat", "distortion", {{"mode", "rat"}});
        append(dry, "cheese", "distortion", {{"mode", "big_cheese"}});
        append(dry, "tape", "distortion", {{"mode", "tape"}});
        append(dry, "wah", "wah", {{"mode", "gcb95"}});
        d = 151;
#ifdef ARDOR_WDW_HAS_CONSOLE_EQ
        append(dry, "eq", "eq", {{"mode", "console_1073"}}); d += 15;
#endif
        append(wet, "ph2", "mod", {{"mode", "phaser_ph2"}});
        append(wet, "room", "reverb", {{"mode", "room"}});
        append(wet, "cloud", "reverb", {{"mode", "cloud"}}); w = 77;
      }
      if (scenario == 4) {
        append(dry, "absent-tape", "distortion", {{"mode", "tape"}}, false, ChainBlockStatus::Disabled);
        append(wet, "plate", "reverb", {{"mode", "plate"}});
      }
      if (scenario == 5) {
        append(dry, "cheese", "distortion", {{"mode", "big_cheese"}}); d = 26;
        append(wet, "ph2", "mod", {{"mode", "phaser_ph2"}}, true); w = 15;
        wet.blocks.back().params["mix"] = 0;
      }
      auto options = buildOptions(quantum);
      // Default derivation must overwrite accidental/stale supplied numbers.
      options.program.dryLatencyFrames = 999; options.program.wetLatencyFrames = 888;
      std::unique_ptr<WdwRoutingProgram> program;
      WdwRoutingBuildReport report;
      std::string error;
      require(buildWdwRoutingProgram(dry, wet, options, program, report, error), error);
      require(report.latencyDerived && report.dryLatencyFrames == d && report.wetLatencyFrames == w,
        "builder declared sums scenario=" + std::to_string(scenario));
      const auto latency = std::max(d, w);
      require(report.totalLatencyFrames == latency && program->latencyFrames() == latency
        && program->alignmentDelayFrames(0) == latency - d
        && program->alignmentDelayFrames(1) == latency - w, "builder compensation direction");
      const auto impulse = renderProgram(*program);
      for (std::size_t i = 0; i < impulse.size(); ++i) {
        const float expected = i == latency ? 2 : 0;
        require(std::abs(impulse[i].left - expected) < 1e-6f
             && std::abs(impulse[i].right - expected) < 1e-6f,
          "builder must align actual lane audio scenario=" + std::to_string(scenario) + " frame=" + std::to_string(i));
      }
      program->reset();
      const auto again = renderProgram(*program);
      for (std::size_t i = 0; i < impulse.size(); ++i)
        require(impulse[i].left == again[i].left && impulse[i].right == again[i].right, "WDW reset changed timing");
      require(program->setBlockEnabled(dry.blocks.front().id, true), "enable dry NAM");
      require(program->setBlockEnabled(wet.blocks.front().id, true), "enable wet NAM");
      require(program->latencyFrames() == latency, "live bypass changed WDW latency");
      options.program.executor.mode = WdwPairExecutionMode::Pipelined;
      require(buildWdwRoutingProgram(dry, wet, options, program, report, error), error);
      require(report.totalLatencyFrames == latency + quantum
           && program->latencyFrames() == latency + quantum
           && program->alignmentDelayFrames(0) == latency - d
           && program->alignmentDelayFrames(1) == latency - w, "common pipeline delay must not change lane compensation");
    }
  }
  auto dry = lane("dry-nam"), wet = lane("wet-nam");
  append(dry, "silent-rat", "distortion", {{"mode", "rat"}, {"volume", 0}}, true);
  auto options = buildOptions();
  options.program.mix = {1, 0, true, 0, 1, false};
  std::unique_ptr<WdwRoutingProgram> program;
  WdwRoutingBuildReport report;
  std::string error;
  require(buildWdwRoutingProgram(dry, wet, options, program, report, error), "silent lane must build: " + error);
  require(report.dryLatencyFrames == 26 && measure(renderProgram(*program)).peak == 0, "silent output must retain declared latency");
  options.deriveLatencies = false;
  options.program.dryLatencyFrames = 123; options.program.wetLatencyFrames = 321;
  require(buildWdwRoutingProgram(dry, wet, options, program, report, error), error);
  require(!report.latencyDerived && report.dryLatencyFrames == 123 && report.wetLatencyFrames == 321
    && program->latencyFrames() == 321 && program->alignmentDelayFrames(0) == 198, "explicit deterministic override");
  options.program.wetLatencyFrames = std::numeric_limits<std::size_t>::max();
  require(!buildWdwRoutingProgram(dry, wet, options, program, report, error) && !program,
    "unaddressable explicit delay must fail without publication");
}

void intentionalTiming()
{
  RuntimeChain ir;
  std::string error;
  require(ir.addIrReverb("ir", {1, .25f}, {}, 48000, error), error);
  ir.setIrReverbParameter("ir", "mix", 1);
  ir.setIrReverbParameter("ir", "preDelayMs", 500);
  const auto m = measure(render(ir, 64, 1, false, 32768));
  require(ir.latencyFrames() == 0 && m.onset == 24000 + IrReverbProcessor::PARTITION_FRAMES,
    "IR user/partition pre-delay must stay intentional");
  std::cerr << "IR reverb 500ms onset=" << *m.onset << " declared=" << ir.latencyFrames() << '\n';
  for (float mix : {0.0f, .25f, 1.0f}) {
    auto dry = lane("dry-nam"), wet = lane("wet-nam");
    append(dry, "rat", "distortion", {{"mode", "rat"}});
    append(wet, "delay", "delay", {{"mode", "digital"}, {"time", .1f}, {"mix", mix}}, true);
    auto options = buildOptions();
    std::unique_ptr<WdwRoutingProgram> program;
    WdwRoutingBuildReport report;
    require(buildWdwRoutingProgram(dry, wet, options, program, report, error), error);
    require(report.dryLatencyFrames == 26 && report.wetLatencyFrames == 0, "delay mix/time must not change compensation");
    const auto output = renderProgram(*program, 16384);
    const float expectedDirect = mix == 1 ? 1 : 2;
    require(std::abs(output[26].left - expectedDirect) < 1e-6f, "delay's dry copy must align with amp lane");
    require(measure(output).onset == 26, "aligned dry contribution timing");
    if (mix == 1) {
      require(program->setMix({0, 0, false, 1, 1, true}), "isolate full-wet delay");
      program->reset();
      const auto wetOnly = measure(renderProgram(*program, 16384));
      require(wetOnly.onset && *wetOnly.onset > 26, "100% wet delay must retain musical delay");
      std::cerr << "full-wet digital delay onset=" << *wetOnly.onset << " declared=0 alignment=26\n";
    }
  }
}

void processedNamAlignment()
{
  auto dry = lane("dry-nam"), wet = lane("wet-nam");
  dry.blocks.front().enabled = wet.blocks.front().enabled = true;
  append(dry, "rat", "distortion", {{"mode", "rat"}});
  append(wet, "ph2", "mod", {{"mode", "phaser_ph2"}, {"mix", 0}}, true);
  auto options = buildOptions();
  std::unique_ptr<WdwRoutingProgram> program;
  WdwRoutingBuildReport report;
  std::string error;
  require(buildWdwRoutingProgram(dry, wet, options, program, report, error), error);
  RuntimeChain reference;
  require(reference.addNam(ARDOR_NAM_EXAMPLE_MODEL, 48000, 64), "load reference NAM");
  const auto amp = render(reference, 64);
  const auto output = renderProgram(*program, kProbeFrames);
  for (std::size_t i = 0; i < output.size(); ++i) {
    const float expected = i >= 26 ? 2 * amp[i - 26].left : 0;
    require(std::abs(output[i].left - expected) < 1e-6f
         && std::abs(output[i].right - expected) < 1e-6f,
      "two processed NAM copies must align with reference at frame " + std::to_string(i));
  }
}

void activeBuilderMetadata(const std::vector<Fixture>& all)
{
  for (const auto& fixture : all) {
    if (!fixture.latency) continue;
    auto dry = lane("dry-nam"), wet = lane("wet-nam");
    std::string type;
    bool isWet = false;
    if (fixture.name.starts_with("mod/")) { type = "mod"; isWet = true; }
    else if (fixture.name.starts_with("reverb/")) { type = "reverb"; isWet = true; }
    else if (fixture.name == "console_eq") type = "eq";
    else if (fixture.name == "wah") type = "wah";
    else type = "distortion";
    append(isWet ? wet : dry, "active-effect", type, fixture.params, true);
    std::unique_ptr<WdwRoutingProgram> program;
    WdwRoutingBuildReport report;
    std::string error;
    require(buildWdwRoutingProgram(dry, wet, buildOptions(), program, report, error), error);
    require(report.dryLatencyFrames == (isWet ? 0 : fixture.latency)
         && report.wetLatencyFrames == (isWet ? fixture.latency : 0),
      fixture.name + " active builder latency must use declaration, not impulse onset");
  }
}

void wetMeasurements()
{
  std::cout << "block,mix,pre_delay,declared,onset,peak,probe_frames\n";
  for (const auto& descriptor : daisyFxCatalog()) {
    if (descriptor.blockType != "reverb" && descriptor.mode != "phaser_ph2") continue;
    for (float mix : {0.0f, .25f, 1.0f}) {
      for (float preDelay : {0.0f, .15f}) {
        auto params = defaultDaisyFxParams(descriptor);
        params["mix"] = mix;
        if (descriptor.blockType == "reverb") params["pre_delay"] = preDelay;
        DaisyFxProcessor p;
        std::string error;
        require(p.configure(descriptor.blockType, params, 48000, error), error);
        const auto declared = p.latencyFrames();
        RuntimeChain chain;
        chain.addDaisy("effect", std::move(p));
        const auto m = measure(render(chain, 64, 1, false, 32768));
        require(chain.latencyFrames() == declared, "wet mix/pre-delay changed declared latency");
        std::cout << descriptor.blockType << '/' << descriptor.mode << ',' << mix << ',' << preDelay
                  << ',' << declared << ',';
        if (m.onset) std::cout << *m.onset;
        std::cout << ',' << m.peakFrame << ",32768\n";
      }
    }
  }
}

std::complex<double> transfer(const Samples& impulse, double hz)
{
  std::complex<double> sum{};
  for (std::size_t i = 0; i < impulse.size(); ++i)
    sum += double(impulse[i].left) * std::polar(1.0, -2 * std::numbers::pi * hz * i / 48000);
  return sum;
}

void combResponse(bool csv = false)
{
  if (csv) std::cout << "wet_relative_db,hz,unaligned_db,aligned_db\n";
  for (double relativeDb : {0.0, -10.0}) {
    const auto ratio = std::pow(10.0, relativeDb / 20);
    std::array<Samples, 2> responses;
    for (bool aligned : {false, true}) {
      auto dry = std::make_unique<RuntimeChain>(), wet = std::make_unique<RuntimeChain>();
      std::string error;
#ifdef ARDOR_WDW_HAS_CONSOLE_EQ
      ConsoleEqProcessor eq;
      require(eq.configure(defaultConsoleEqParams(), 48000, error), error);
      dry->addConsoleEq("latency-block", std::move(eq));
#else
      DaisyFxProcessor ph2;
      require(ph2.configure("mod", {{"mode", "phaser_ph2"}, {"mix", 0}}, 48000, error), error);
      dry->addDaisy("latency-block", std::move(ph2));
#endif
      dry->setBlockEnabled("latency-block", false);
      dry->reset();
      auto options = buildOptions().program;
      options.executor.blockSize = 64;
      options.executor.sampleRate = 48000;
      options.mix.wetLevel = ratio;
      options.dryLatencyFrames = aligned ? dry->latencyFrames() : 0;
      options.wetLatencyFrames = wet->latencyFrames();
      WdwRoutingProgram program;
      require(program.prepare({"dry", std::move(dry), -1}, {"wet", std::move(wet), -1}, options, error), error);
      responses[aligned] = renderProgram(program);
    }
    for (double hz = 0; hz <= 20000; hz += 25) {
      const auto unaligned = std::abs(transfer(responses[0], hz));
      const auto aligned = std::abs(transfer(responses[1], hz));
      const auto expected = std::abs(std::polar(1.0, -2 * std::numbers::pi * hz * 15 / 48000)
                                  + std::complex<double>{ratio, 0});
      require(std::abs(unaligned - expected) < 2e-6, "rendered WDW comb does not match two-copy response");
      require(std::abs(aligned - (1 + ratio)) < 2e-6, "declared latency must remove comb across audio band");
      if (csv) std::cout << relativeDb << ',' << hz << ',' << 20 * std::log10(std::max(unaligned, 1e-12))
                        << ',' << 20 * std::log10(aligned) << '\n';
    }
    for (double notch : {1600.0, 4800.0, 8000.0}) {
      const auto gain = std::abs(transfer(responses[0], notch));
      if (relativeDb == 0) require(gain < 1e-6, "equal-level comb notch");
      else require(std::abs(20 * std::log10(gain) + 3.30177) < .001, "-10dB wet comb trough");
    }
    require(std::abs(20 * std::log10(std::abs(transfer(responses[0], 0)))
      - (relativeDb == 0 ? 6.02060 : 2.38662)) < .001, "comb crest gain");
  }
}

void parameterMeasurements()
{
  std::cout << "block,control_value,amplitude,threshold,declared,onset,peak\n";
  for (float knob : {.0f, .25f, .5f, .7f, 1.0f}) {
    for (float amplitude : {.001f, .01f, .1f, 1.0f}) {
      for (auto fixture : {
        effect<RatProcessor>("rat", 26, {{"mode", "rat"}, {"distortion", knob}}),
        effect<CheeseProcessor>("big_cheese", 26, {{"mode", "big_cheese"}, {"fuzz", knob}}),
        effect<TapeProcessor>("tape", 76, {{"mode", "tape"}, {"saturation", knob}})}) {
        RuntimeChain chain;
        fixture.add(chain);
        const auto response = render(chain, 64, amplitude);
        for (float threshold : {1e-10f, 1e-7f, 1e-4f}) {
          const auto m = measure(response, threshold);
          require(chain.latencyFrames() == fixture.latency, "knob/amplitude/threshold changed declaration");
          std::cout << fixture.name << ',' << knob << ',' << amplitude << ',' << threshold
                    << ',' << fixture.latency << ',';
          if (m.onset) std::cout << *m.onset;
          std::cout << ',' << m.peakFrame << '\n';
        }
      }
    }
  }
}

} // namespace

int main(int argc, char** argv)
{
  try {
    const auto all = fixtures();
    if (argc == 2 && std::string(argv[1]) == "--measure-parameters") { parameterMeasurements(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--measure-spectrum") { combResponse(true); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--measure-wet") { wetMeasurements(); return 0; }
    measurements(all);
    if (argc == 2 && std::string(argv[1]) == "--measure-only") return 0;
    bypassContracts(all);
    impulseContracts(all);
    bypassTransitions(all);
    mixEndpoints(all);
    halfbandContracts();
    builderContracts();
    activeBuilderMetadata(all);
    intentionalTiming();
    processedNamAlignment();
    combResponse();
    std::cerr << "WDW latency contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "WDW latency regression: " << error.what() << '\n';
    return 1;
  }
}
