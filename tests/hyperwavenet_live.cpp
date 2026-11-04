// Hardware qualification helper; deliberately not registered with CTest.
#include "audio/EngineLoader.h"
#include "audio/MiniaudioBackend.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

int main(int argc, char **argv) try {
  if (argc != 4)
    throw std::runtime_error("usage: pedal-hyperwavenet-live PRESET DATA_ROOT SECONDS");
  const int seconds = std::stoi(argv[3]);
  if (seconds < 1 || seconds > 3600)
    throw std::runtime_error("duration must be 1 to 3600 seconds");
  const std::filesystem::path root = argv[2];
  std::ifstream input(argv[1]);
  const auto document = nlohmann::json::parse(input);
  const auto preset = ardor::presetFromJson(document);
  const auto block = std::find_if(preset.blocks.begin(), preset.blocks.end(),
                                  [](const auto &b) { return b.enabled && b.type == "nam"; });
  if (block == preset.blocks.end())
    throw std::runtime_error("preset must include a standalone NAM amp");
  std::ifstream modelFile(root / block->asset);
  const auto model = nlohmann::json::parse(modelFile);
  if (model.at("architecture") != "HyperWaveNet")
    throw std::runtime_error("requires a HyperWaveNet model");
  const auto definitions = model.at("config").at("params");
  ardor::PedalEngine engine;
  ardor::EngineLoadOptions load;
  load.blockSize = 128;
  std::string error;
  if (!ardor::applyPreset(engine, preset, root, load, error))
    throw std::runtime_error(error);
  ardor::MiniaudioBackend audio;
  audio.setOutputMuted(true);
  ardor::RealtimeOptions options;
  options.blockSize = 128;
  options.audioCpu = 2;
  options.requireRealtimeScheduler = true;
  options.requireNativeSampleRate = true;
  if (!audio.start(engine, options))
    throw std::runtime_error(audio.lastError());
  std::cout << std::unitbuf << "Live HD + cabinet, muted output, all controls edited every 100 ms\n";
  const auto start = std::chrono::steady_clock::now();
  size_t updates = 0;
  for (int tick = 0; tick < seconds * 10; ++tick) {
    const auto deadline = start + std::chrono::milliseconds((tick + 1) * 100);
    for (size_t i = 0; i < definitions.size(); ++i) {
      const auto &s = definitions[i];
      const float lo = s.at("min").get<float>(), hi = s.at("max").get<float>();
      const float value = s.value("type", std::string{"continuous"}) == "switch"
                              ? lo + static_cast<int>(tick + i) % static_cast<int>(hi - lo + 1)
                              : lo + (hi - lo) * ((tick + i * 13) % 101) / 100.f;
      if (!engine.setNamParameter(block->id, s.at("name").get<std::string>(), value))
        throw std::runtime_error("live target rejected");
      ++updates;
    }
    std::this_thread::sleep_until(deadline);
    if (audio.deviceStopped())
      throw std::runtime_error("audio device stopped");
    if (tick % 100 == 99 || tick == seconds * 10 - 1) {
      const auto s = audio.stats();
      std::cout << "seconds=" << (tick + 1) / 10 << ",callbacks=" << s.callbacks << ",over=" << s.overBudget
                << ",gaps=" << s.callbackGaps << ",avg_ms=" << s.averageMs << ",max_ms=" << s.maxMs
                << ",budget_ms=" << s.budgetMs << ",updates=" << updates
                << ",nonfinite=" << engine.nonFiniteBlockCount()
                << ",mismatch=" << engine.blockSizeMismatchCount() << "\n";
    }
  }
  const auto stats = audio.stats();
  audio.stop();
  if (!ardor::hasRequiredRealtimeScheduler(stats) || stats.affinitySetupError || stats.overBudget ||
      stats.callbackGaps || engine.nonFiniteBlockCount() || engine.blockSizeMismatchCount())
    throw std::runtime_error("hardware qualification failed");
  std::cout << "Hardware control sweep passed\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
