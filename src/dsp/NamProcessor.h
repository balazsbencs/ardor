#pragma once

#include <filesystem>
#include <memory>
#include <vector>

namespace nam {
class DSP;
}

namespace ardor {

class NamProcessor {
public:
  NamProcessor();
  ~NamProcessor();

  bool load(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize);
  float process(float input);
  bool loaded() const;

private:
  std::unique_ptr<nam::DSP> model_;
  std::vector<float> input_{0.0f};
  std::vector<float> output_{0.0f};
};

} // namespace ardor
