// SPDX-License-Identifier: MIT
#pragma once
#include "ModelInfo.h"
#include <functional>

namespace ardor::nam_clap {
struct EditorState {
  const ModelInfo* model = nullptr;
  std::string status;
  uint64_t revision = 0;
  bool loading = false;
  float inputPeak = 0, outputPeak = 0;
};
struct EditorCallbacks {
  std::function<EditorState()> state;
  std::function<double(uint32_t)> value;
  std::function<void(uint32_t,double)> change;
  std::function<void(uint32_t,bool)> gesture;
  std::function<void(std::filesystem::path)> open;
  std::function<void()> tick;
};
class Editor {
public:
  virtual ~Editor() = default;
  virtual bool parent(void*) = 0;
  virtual bool resize(uint32_t,uint32_t) = 0;
  virtual bool show(bool) = 0;
  virtual uint32_t width() const = 0;
  virtual uint32_t height() const = 0;
};
std::unique_ptr<Editor> createEditor(EditorCallbacks);
} // namespace ardor::nam_clap
