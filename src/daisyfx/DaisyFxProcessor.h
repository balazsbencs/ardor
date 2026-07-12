#pragma once

#include <nlohmann/json.hpp>

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
  void reset();
  StereoSample process(StereoSample input);

private:
  DaisyFxProcessor(const DaisyFxProcessor&) = delete;
  DaisyFxProcessor& operator=(const DaisyFxProcessor&) = delete;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ardor
