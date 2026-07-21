#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <memory>
#include <string>

namespace ardor {

struct StereoSample {
  float left = 0.0f;
  float right = 0.0f;
};

class DaisyFxProcessor {
public:
  DaisyFxProcessor();
  ~DaisyFxProcessor();
  DaisyFxProcessor(DaisyFxProcessor&&) noexcept;
  DaisyFxProcessor& operator=(DaisyFxProcessor&&) noexcept;

  bool configure(const std::string& blockType, const nlohmann::json& params,
                 float sampleRate, std::string& error);
  // Control-thread safe: publishes a normalized continuous target. The audio
  // thread consumes it at the vendor's existing 48-sample control cadence.
  bool setParameterTarget(const std::string& key, float normalized);
  void reset();
  StereoSample process(StereoSample input);
  // Fixed algorithmic latency introduced by the 48 kHz <-> 24 kHz reverb
  // boundary. Hosts can use this for chain/offline alignment.
  size_t latencyFrames() const noexcept;
  // Offline rendering estimate. Delay/reverb estimates are capped at 60 s;
  // callers may request a longer explicit tail when preserving extreme
  // feedback is important.
  size_t tailFrames() const noexcept;

private:
  DaisyFxProcessor(const DaisyFxProcessor&) = delete;
  DaisyFxProcessor& operator=(const DaisyFxProcessor&) = delete;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ardor
