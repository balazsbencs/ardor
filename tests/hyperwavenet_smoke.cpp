#include "dsp/NamProcessor.h"
#include "namparam/parametric_container.h"
#include "namparam/wavenet/concat_wavenet.h"
#include "namparam/wavenet/hyperwavenet.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <vector>
#ifdef __linux__
#include <sched.h>
#endif

thread_local bool audit = false;
thread_local size_t allocations = 0, frees = 0;
void *operator new(size_t n) {
  if (audit)
    ++allocations;
  if (auto *p = std::malloc(std::max<size_t>(n, 1)))
    return p;
  throw std::bad_alloc();
}
void *operator new[](size_t n) {
  return ::operator new(n);
}
void operator delete(void *p) noexcept {
  if (audit && p)
    ++frees;
  std::free(p);
}
void operator delete[](void *p) noexcept {
  ::operator delete(p);
}
void operator delete(void *p, size_t) noexcept {
  ::operator delete(p);
}
void operator delete[](void *p, size_t) noexcept {
  ::operator delete(p);
}

#ifdef ARDOR_AUDIT_C_ALLOCATIONS
extern "C" {
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
void *__wrap_malloc(size_t n) {
  if (audit)
    ++allocations;
  return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t s) {
  if (audit)
    ++allocations;
  return __real_calloc(n, s);
}
void *__wrap_realloc(void *p, size_t n) {
  if (audit)
    ++allocations;
  return __real_realloc(p, n);
}
void __wrap_free(void *p) {
  if (audit && p)
    ++frees;
  __real_free(p);
}
}
#endif

using Json = nlohmann::json;
void require(bool ok, const char *what) {
  if (!ok)
    throw std::runtime_error(what);
}
Json read(const std::filesystem::path &p) {
  std::ifstream f(p);
  return Json::parse(f);
}
std::vector<float> render(nam::DSP &dsp, const std::vector<float> &input, int block) {
  dsp.ResetAndPrewarm(48000, block);
  std::vector<float> output(input.size()), x(block), y(block);
  for (size_t i = 0; i < input.size(); i += block) {
    const int n = std::min<size_t>(block, input.size() - i);
    std::copy_n(input.data() + i, n, x.data());
    auto *xp = x.data();
    auto *yp = y.data();
    dsp.process(&xp, &yp, n);
    std::copy_n(y.data(), n, output.data() + i);
  }
  return output;
}
void parity(const std::filesystem::path &dir) {
  const auto doc = read(dir / "hyperwavenet.nam"), golden = read(dir / "hyperwavenet_golden.json");
  const auto input = golden.at("input").get<std::vector<float>>();
  float worst = 0;
  for (int block : {13, 64, 128}) {
    auto dsp = nam::ardor_parametric::create(doc);
    auto &controls = dynamic_cast<nam::IParametricControl &>(*dsp);
    for (const auto &setting : golden.at("settings")) {
      const auto values = setting.at("params").get<std::vector<float>>();
      controls.SetParams(values);
      const auto output = render(*dsp, input, block),
                 expected = setting.at("output").get<std::vector<float>>();
      for (size_t i = 0; i < output.size(); ++i) {
        require(std::isfinite(output[i]), "finite golden audio");
        worst = std::max(worst, std::fabs(output[i] - expected[i]));
      }
    }
  }
  require(worst < 1e-5f, "Python reference parity");
  std::cout << "Python reference max error: " << worst << " (13/64/128 frames)\n";
  for (int kind = 0; kind < 10; ++kind) {
    auto bad = doc;
    switch (kind) {
    case 0:
      bad["weights"].erase(bad["weights"].end() - 1);
      break;
    case 1:
      bad["weights"][0] = std::numeric_limits<float>::infinity();
      break;
    case 2:
      bad["config"]["hypernet"]["delta_map"][0]["export_offset"] = 1000000;
      break;
    case 3:
      bad["config"]["hypernet"]["delta_map"][1]["export_offset"] = 0;
      break;
    case 4:
      bad["config"]["hypernet"]["delta_map"][3]["out_features"] = 7;
      break;
    case 5:
      bad["config"]["hypernet"]["hidden_sizes"] = {1000000000};
      break;
    case 6:
      bad["config"]["hypernet"]["mode"] = "unknown";
      break;
    case 7:
      bad["config"]["condition_dsp"] = Json::object();
      break;
    case 8:
      bad["version"] = "0.7.0";
      break;
    case 9:
      bad["config"]["layers"][0]["slimmable"] = Json::object();
      break;
    }
    bool rejected = false;
    try {
      auto dsp = nam::ardor_parametric::create(bad);
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "reject malformed HyperWaveNet");
  }
  std::cout << "Malformed model checks: passed\n";
}
Json syntheticA2() {
  Json config = {
      {"layers", Json::array({{{"input_size", 1},
                               {"condition_size", 1},
                               {"channels", 16},
                               {"bottleneck", 16},
                               {"head", {{"out_channels", 1}, {"kernel_size", 16}, {"bias", true}}},
                               {"kernel_sizes",
                                {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6}},
                               {"dilations", {1,   3,   7, 17, 41, 101, 239, 1,  3,  7,   17, 41,
                                              101, 239, 1, 13, 1,  3,   7,   17, 41, 101, 239}},
                               {"activation", Json::array()},
                               {"layer1x1", {{"active", true}, {"groups", 1}}},
                               {"head1x1", {{"active", false}, {"out_channels", 1}, {"groups", 1}}}}})},
      {"head", nullptr},
      {"head_scale", 0.01},
      {"params",
       Json::array({{{"name", "Gain"}, {"type", "continuous"}, {"min", 0}, {"max", 10}, {"default", 5}}})},
      {"hypernet",
       {{"hidden_sizes", {4}},
        {"activation", "LeakyReLU"},
        {"mode", "low_rank"},
        {"rank", 2},
        {"delta_map",
         Json::array({{{"name", "rechannel"}, {"mode", "full"}, {"numel", 16}, {"export_offset", 0}},
                      {{"name", "conv"}, {"mode", "full"}, {"numel", 1}, {"export_offset", 16}},
                      {{"name", "residual"},
                       {"mode", "low_rank"},
                       {"numel", 256},
                       {"rank", 2},
                       {"out_features", 16},
                       {"rest_features", 16},
                       {"export_offset", 1584}}})}}}};
  for (int i = 0; i < 23; ++i)
    config["layers"][0]["activation"].push_back({{"type", "LeakyReLU"}, {"negative_slope", 0.01}});
  auto parsed = nam::wavenet::create_hyperwavenet_config(config, 48000);
  auto &hyper = dynamic_cast<nam::wavenet::HyperWaveNetConfig &>(*parsed);
  const size_t count = nam::wavenet::wave_net_weight_count(hyper.inner) +
                       nam::Hypernetwork::SerializedStateCount(hyper.hypernet, hyper.params);
  std::vector<float> weights(count);
  uint32_t random = 713;
  for (float &w : weights) {
    random = random * 1664525u + 1013904223u;
    w = (static_cast<int>(random >> 16) - 32768) * 0.000001f;
  }
  weights[nam::wavenet::wave_net_weight_count(hyper.inner) - 1] = 0.01f;
  return {{"version", "1.0.0"},
          {"architecture", "HyperWaveNet"},
          {"sample_rate", 48000},
          {"config", config},
          {"weights", weights}};
}
void fastParity(const Json &doc, const std::string &label) {
  auto parsed = nam::wavenet::create_hyperwavenet_config(doc.at("config"), 48000);
  auto &genericConfig = dynamic_cast<nam::wavenet::HyperWaveNetConfig &>(*parsed);
  if (!nam::wavenet::MutableA2::supports(genericConfig.inner))
    return;
  // Near-matching architectures must retain the generic implementation.
  for (int changed = 0; changed < 6; ++changed) {
    auto different = doc.at("config");
    auto &layer = different["layers"][0];
    switch (changed) {
    case 0:
      layer["activation"][0] = {{"type", "Tanh"}};
      break;
    case 1:
      layer["head"]["kernel_size"] = 15;
      break;
    case 2:
      layer["head"]["bias"] = false;
      break;
    case 3:
      layer["layer1x1"]["groups"] = 2;
      break;
    case 4:
      layer["dilations"][0] = 2;
      break;
    case 5:
      layer["kernel_sizes"][0] = 7;
      break;
    }
    auto config = nam::wavenet::create_hyperwavenet_config(different, 48000);
    require(
        !nam::wavenet::MutableA2::supports(dynamic_cast<nam::wavenet::HyperWaveNetConfig &>(*config).inner),
        "non-A2 architecture keeps generic runtime");
  }
  genericConfig.enableFastA2 = false;
  auto slow = parsed->create(doc.at("weights").get<std::vector<float>>(), 48000);
  auto fastConfig = nam::wavenet::create_hyperwavenet_config(doc.at("config"), 48000);
  dynamic_cast<nam::wavenet::HyperWaveNetConfig &>(*fastConfig).enableFastA2 = true;
  auto fast = fastConfig->create(doc.at("weights").get<std::vector<float>>(), 48000);
  require(dynamic_cast<nam::wavenet::HyperWaveNet &>(*fast).usesFastA2(),
          "16-channel topology selects optimized path");
  auto &fc = dynamic_cast<nam::IParametricControl &>(*fast);
  auto &sc = dynamic_cast<nam::IParametricControl &>(*slow);
  std::vector<float> values(fc.GetParams().begin(), fc.GetParams().end());
  float worst = 0, peak = 0;
  double squared = 0, energy = 0;
  size_t count = 0;
  for (int block : {13, 64, 128, 129}) {
    fast->ResetAndPrewarm(48000, block);
    slow->ResetAndPrewarm(48000, block);
    std::vector<float> x(block), a(block), b(block);
    for (int n = 0; n < 160; ++n) {
      const int frames = n % 11 == 0 ? std::max(1, block - 7) : block;
      if (n % 23 == 0) {
        for (size_t i = 0; i < values.size(); ++i) {
          const auto &s = fc.GetParamSpecs()[i];
          values[i] = s.type == "switch" ? static_cast<float>((n / 23 + i) % s.enum_names.size())
                                         : s.min + (s.max - s.min) * ((n / 23 + i) % 11) / 10.f;
        }
        fc.SetParams(values);
        sc.SetParams(values);
      }
      for (int i = 0; i < block; ++i)
        x[i] = 0.05f * std::sin((n * block + i) * 0.037f) + 0.017f * std::cos((n * block + i) * 0.187f);
      auto *xp = x.data();
      auto *ap = a.data();
      auto *bp = b.data();
      fast->process(&xp, &ap, frames);
      slow->process(&xp, &bp, frames);
      for (int i = 0; i < frames; ++i) {
        require(std::isfinite(a[i]) && std::isfinite(b[i]), "finite optimized/reference audio");
        const float e = std::fabs(a[i] - b[i]);
        worst = std::max(worst, e);
        peak = std::max(peak, std::fabs(b[i]));
        squared += e * e;
        energy += b[i] * b[i];
        ++count;
      }
    }
  }
  const double rms = std::sqrt(squared / count), referenceRms = std::sqrt(energy / count);
  std::cout << label << " optimized/reference max_error=" << worst << ",peak=" << peak << ",error_rms=" << rms
            << ",reference_rms=" << referenceRms << "\n";
  require(worst < 1e-5f + peak * 1e-4f && rms < 1e-6 + referenceRms * 1e-4,
          "optimized audio matches generic WaveNet");
}
void exercise(const std::filesystem::path &path) {
  const auto doc = read(path);
  fastParity(doc, path.filename().string());
  ardor::NamProcessor processor;
  require(processor.load(path, 48000, 128), "production model load");
  auto reference = nam::ardor_parametric::create(doc);
  auto &controls = dynamic_cast<nam::IParametricControl &>(*reference);
  auto values = std::vector<float>(controls.GetParams().begin(), controls.GetParams().end());
  std::array<float, 128> input{}, output{};
  std::vector<double> steady, changed;
  steady.reserve(10000);
  changed.reserve(1000);
  double checksum = 0;
  bool accepted = true, finite = true;
  for (size_t b = 0; b < 11000; ++b) {
    for (size_t i = 0; i < input.size(); ++i)
      input[i] = 0.03f * std::sin((b * 128 + i) * 0.071f);
    const bool edit = b >= 1000 && b % 16 == 0;
    audit = true;
    const auto start = std::chrono::steady_clock::now();
    if (edit)
      for (size_t i = 0; i < values.size(); ++i) {
        const auto &s = controls.GetParamSpecs()[i];
        values[i] = s.type == "switch" ? static_cast<float>((b / 16) % s.enum_names.size())
                                       : s.min + (s.max - s.min) * ((b / 16) % 101) / 100.f;
        accepted &= processor.setParameterTarget(s.name, values[i]);
      }
    processor.processBlock(input.data(), output.data(), input.size());
    const auto elapsed =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    audit = false;
    if (b >= 1000)
      (edit ? changed : steady).push_back(elapsed);
    for (float v : output) {
      finite &= std::isfinite(v);
      checksum += v;
    }
  }
  require(accepted && finite, "live controls and audio remain valid");
  require(allocations == 0 && frees == 0, "no C/C++ allocation/deallocation in live audio");
  // After live edits, reset must retain the latest values and settle on the same
  // stream as a freshly restored preset, including calibration/normalization.
  Json saved = Json::object();
  for (size_t i = 0; i < values.size(); ++i)
    saved[controls.GetParamSpecs()[i].name] = values[i];
  ardor::NamProcessor restored;
  require(restored.load(path, 48000, 128, 1, std::nullopt, saved), "restore controls");
  processor.reset();
  std::array<float, 128> other{};
  processor.processBlock(input.data(), output.data(), 128);
  restored.processBlock(input.data(), other.data(), 128);
  // Production bypass resets deliberately clear history without prewarming, so
  // compare after enough identical input to replace the entire receptive field.
  for (int i = 0; i < 128; ++i) {
    processor.processBlock(input.data(), output.data(), 128);
    restored.processBlock(input.data(), other.data(), 128);
  }
  float error = 0;
  for (size_t i = 0; i < 128; ++i)
    error = std::max(error, std::fabs(other[i] - output[i]));
  require(error < 1e-5f, "saved controls restore identically");
  require(
      !processor.setParameterTarget(controls.GetParamSpecs()[0].name, controls.GetParamSpecs()[0].max + 1),
      "reject out-of-range target");
  auto stats = [&](const char *mode, std::vector<double> &samples) {
    std::sort(samples.begin(), samples.end());
    std::cout << path.filename() << "," << mode << ",n=" << samples.size()
              << ",p99.9_us=" << samples[static_cast<size_t>(std::ceil((samples.size() - 1) * 0.999))]
              << ",deadline_misses="
              << std::count_if(samples.begin(), samples.end(), [](double us) { return us > 128e6 / 48000; })
              << ",max_us=" << samples.back() << "\n";
  };
  stats("steady", steady);
  stats("all-controls-change", changed);
  std::cout << "Live controls, preset restore and allocation audit passed; checksum=" << checksum << "\n";
}
int main(int argc, char **argv) try {
  int first = 1;
#ifdef __linux__
  if (argc > 2 && std::string(argv[1]) == "--cpu") {
    size_t used = 0;
    const int cpu = std::stoi(argv[2], &used);
    require(used == std::string(argv[2]).size() && cpu >= 0 && cpu < CPU_SETSIZE, "valid CPU index");
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    require(sched_setaffinity(0, sizeof(mask), &mask) == 0, "CPU affinity");
    first = 3;
  }
#endif
  const auto fixtures = argc > first
                            ? std::filesystem::path(argv[first])
                            : std::filesystem::path(ARDOR_SOURCE_DIR) / "tests/fixtures/hyperwavenet";
  std::cout << std::unitbuf;
  parity(fixtures);
  fastParity(syntheticA2(), "synthetic A2");
  exercise(fixtures / "hyperwavenet.nam");
  for (int i = first + 1; i < argc; ++i)
    exercise(argv[i]);
} catch (const std::exception &e) {
  audit = false;
  std::cerr << e.what() << "\n";
  return 1;
}
