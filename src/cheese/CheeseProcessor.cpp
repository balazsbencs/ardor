#include "cheese/CheeseProcessor.h"

#include "cheese/CheeseNetlist.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace ardor {

namespace {

float configuredNumber(const nlohmann::json& params, const char* key, float fallback)
{
  if (!params.is_object()) return fallback;
  const auto it = params.find(key);
  if (it == params.end() || !it->is_number()) return fallback;
  const float value = it->get<float>();
  return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
}

} // namespace

struct CheeseControlWorker {
  enum SlotState : unsigned { Free, Writing, Ready, Reading };

  struct Slot {
    std::atomic<unsigned> state{Free};
    std::uint64_t generation = 0;
    float fuzz = 0.7f;
    float tone = 0.5f;
    CheesePreparedMatrices matrices{};
  };

  CheeseControlWorker(const CheeseNetlist& sourceNetlist, float oversampledRate,
                      float fuzz, float tone, float volumeGain)
    : netlist(sourceNetlist), sampleRate(oversampledRate), fuzzTarget(fuzz),
      toneTarget(tone), volumeGainTarget(volumeGain), thread([this] { run(); })
  {
  }

  ~CheeseControlWorker()
  {
    stopping.store(true, std::memory_order_release);
    wake.notify_one();
    if (thread.joinable()) thread.join();
  }

  void signalRequest()
  {
    requestedGeneration.fetch_add(1, std::memory_order_release);
    wake.notify_one();
  }

  void requestFuzz(float fuzz)
  {
    fuzzTarget.store(fuzz, std::memory_order_relaxed);
    signalRequest();
  }

  void requestTone(float tone)
  {
    toneTarget.store(tone, std::memory_order_relaxed);
    signalRequest();
  }

  void run()
  {
    std::uint64_t handled = 0;
    while (!stopping.load(std::memory_order_acquire)) {
      std::uint64_t generation = requestedGeneration.load(std::memory_order_acquire);
      if (generation == handled) {
        std::unique_lock lock(waitMutex);
        wake.wait(lock, [&] {
          return stopping.load(std::memory_order_acquire)
            || requestedGeneration.load(std::memory_order_acquire) != handled;
        });
        continue;
      }

      float fuzz = 0.7f;
      float tone = 0.5f;
      do {
        generation = requestedGeneration.load(std::memory_order_acquire);
        fuzz = fuzzTarget.load(std::memory_order_relaxed);
        tone = toneTarget.load(std::memory_order_relaxed);
      } while (generation != requestedGeneration.load(std::memory_order_acquire));

      CheesePreparedMatrices prepared;
      try {
        prepared = prepareCheeseCircuitMatrices(netlist, fuzz, tone, sampleRate);
      } catch (...) {
        handled = generation;
        derivationFailures.fetch_add(1, std::memory_order_relaxed);
        continue;
      }

      // A newer request makes this result obsolete. Coalescing here prevents a
      // fast encoder turn from filling the handoff slots with stale positions.
      if (generation != requestedGeneration.load(std::memory_order_acquire)) continue;

      Slot* destination = nullptr;
      while (!destination && !stopping.load(std::memory_order_acquire)) {
        for (auto& slot : slots) {
          unsigned expected = Free;
          if (slot.state.compare_exchange_strong(expected, Writing,
                                                 std::memory_order_acq_rel)) {
            destination = &slot;
            break;
          }
        }
        if (generation != requestedGeneration.load(std::memory_order_acquire)) break;
        if (!destination) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      if (!destination) {
        if (stopping.load(std::memory_order_acquire)) break;
        continue;
      }

      destination->matrices = prepared;
      destination->generation = generation;
      destination->fuzz = fuzz;
      destination->tone = tone;
      destination->state.store(Ready, std::memory_order_release);
      publishedGeneration.store(generation, std::memory_order_release);
      handled = generation;
    }
  }

  CheeseNetlist netlist{};
  float sampleRate = 384000.0f;
  std::atomic<float> fuzzTarget{0.7f};
  std::atomic<float> toneTarget{0.5f};
  std::atomic<float> volumeGainTarget{0.7f};
  std::atomic<std::uint64_t> requestedGeneration{0};
  std::atomic<std::uint64_t> publishedGeneration{0};
  std::atomic<std::uint64_t> appliedGeneration{0};
  std::atomic<unsigned long long> derivationFailures{0};
  std::array<Slot, 3> slots{};
  std::atomic<bool> stopping{false};
  std::mutex waitMutex;
  std::condition_variable wake;
  std::thread thread;
};

CheeseProcessor::~CheeseProcessor() = default;
CheeseProcessor::CheeseProcessor(CheeseProcessor&&) noexcept = default;
CheeseProcessor& CheeseProcessor::operator=(CheeseProcessor&&) noexcept = default;

bool CheeseProcessor::configure(const nlohmann::json& params, float sampleRate, std::string& error)
{
  control_.reset();
  error.clear();
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
    error = "big cheese sample rate must be finite and positive";
    return false;
  }
  const auto mode = params.value("mode", std::string{"big_cheese"});
  if (mode != "big_cheese") {
    error = "unsupported fuzz mode: " + mode;
    return false;
  }
  if (!cheeseNetlistValid(cheeseNetlist())) {
    error = "big cheese netlist holds a component value that is not realizable";
    return false;
  }

  sampleRate_ = sampleRate;
  const float fuzz = configuredNumber(params, "fuzz", 0.7f);
  const float tone = configuredNumber(params, "tone", 0.5f);
  const float volume = configuredNumber(params, "volume", 0.7f);
  const float volumeGain = static_cast<float>(
    std::pow(static_cast<double>(volume), cheeseNetlist().taperExponent));

  constexpr float kSmoothingSeconds = 0.015f;
  smoothing_ = 1.0f - std::exp(-1.0f / (kSmoothingSeconds * sampleRate_));

  circuit_.init(cheeseNetlist(), sampleRate_ * 8.0f, fuzz, tone, volume);
  resetResamplers();
  volumeGain_ = volumeGain;
  control_ = std::make_shared<CheeseControlWorker>(
    cheeseNetlist(), sampleRate_ * 8.0f, fuzz, tone, volumeGain);
  return true;
}

bool CheeseProcessor::setParameterTarget(const std::string& key, float value)
{
  if (!std::isfinite(value)) return false;
  if (!control_) return false;
  const float clamped = std::clamp(value, 0.0f, 1.0f);
  if (key == "fuzz") {
    control_->requestFuzz(clamped);
  } else if (key == "tone") {
    control_->requestTone(clamped);
  } else if (key == "volume") {
    const float gain = static_cast<float>(
      std::pow(static_cast<double>(clamped), cheeseNetlist().taperExponent));
    control_->volumeGainTarget.store(gain, std::memory_order_relaxed);
  } else {
    return false;
  }
  return true;
}

void CheeseProcessor::resetResamplers() noexcept
{
  up2x_.Reset();
  up4x_.Reset();
  up8x_.Reset();
  down4x_.Reset();
  down2x_.Reset();
  down1x_.Reset();
}

void CheeseProcessor::consumePreparedControls() noexcept
{
  if (!control_) return;
  const std::uint64_t applied = control_->appliedGeneration.load(std::memory_order_relaxed);
  if (control_->publishedGeneration.load(std::memory_order_acquire) <= applied) return;

  CheeseControlWorker::Slot* selected = nullptr;
  std::uint64_t newest = applied;
  for (auto& slot : control_->slots) {
    if (slot.state.load(std::memory_order_acquire) == CheeseControlWorker::Ready
        && slot.generation > newest) {
      newest = slot.generation;
      selected = &slot;
    }
  }
  if (!selected) return;

  unsigned expected = CheeseControlWorker::Ready;
  if (!selected->state.compare_exchange_strong(expected, CheeseControlWorker::Reading,
                                                std::memory_order_acq_rel)) {
    return;
  }
  const std::uint64_t selectedGeneration = selected->generation;
  circuit_.applyPreparedMatrices(selected->matrices, selected->fuzz, selected->tone);
  control_->appliedGeneration.store(selectedGeneration, std::memory_order_release);
  selected->state.store(CheeseControlWorker::Free, std::memory_order_release);

  // Drop any superseded ready result without reading its payload.
  for (auto& slot : control_->slots) {
    if (slot.state.load(std::memory_order_acquire) == CheeseControlWorker::Ready
        && slot.generation <= selectedGeneration) {
      expected = CheeseControlWorker::Ready;
      (void)slot.state.compare_exchange_strong(expected, CheeseControlWorker::Free,
                                               std::memory_order_acq_rel);
    }
  }
}

void CheeseProcessor::reset()
{
  consumePreparedControls();
  resetResamplers();
  if (control_) {
    volumeGain_ = control_->volumeGainTarget.load(std::memory_order_relaxed);
    circuit_.setVolumeGain(volumeGain_);
  }
  circuit_.reset();
}

StereoSample CheeseProcessor::process(StereoSample input)
{
  consumePreparedControls();
  if (control_) {
    const float targetGain = control_->volumeGainTarget.load(std::memory_order_relaxed);
    volumeGain_ += smoothing_ * (targetGain - volumeGain_);
    circuit_.setVolumeGain(volumeGain_);
  }

  const float mono = (input.left + input.right) * 0.5f;
  const auto at2x = up2x_.Process(mono);
  float at2xFiltered[2]{};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto at4x = up4x_.Process(at2x[i]);
    for (const float quarterRate : at4x) {
      const auto at8x = up8x_.Process(quarterRate);
      float at4xFiltered = 0.0f;
      for (const float sample : at8x) {
        const float processed = circuit_.process(sample);
        float decimated = 0.0f;
        if (down4x_.Push(processed, decimated)) at4xFiltered = decimated;
      }
      float decimated = 0.0f;
      if (down2x_.Push(at4xFiltered, decimated)) at2xFiltered[i] = decimated;
    }
  }
  float output = 0.0f;
  for (const float sample : at2xFiltered) (void)down1x_.Push(sample, output);
  return {output, output};
}

bool CheeseProcessor::controlUpdatePending() const noexcept
{
  return control_
    && control_->requestedGeneration.load(std::memory_order_acquire)
      > control_->appliedGeneration.load(std::memory_order_acquire);
}

unsigned long long CheeseProcessor::controlDerivationFailures() const noexcept
{
  return control_ ? control_->derivationFailures.load(std::memory_order_relaxed) : 0;
}

} // namespace ardor
