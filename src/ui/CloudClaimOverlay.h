#pragma once

#include <chrono>
#include <filesystem>
#include <string>

#include <lvgl.h>

namespace ardor {

class CloudClaimOverlay {
public:
  explicit CloudClaimOverlay(std::filesystem::path dataRoot);
  ~CloudClaimOverlay();

  void poll();
  bool active() const { return mode_ != Mode::Hidden; }
  bool handleFootswitch(int index);
  bool recordPhysicalDecision(bool approved, std::string& error);

private:
  enum class Mode { Hidden, Code, Pending, Decided, LocalSetup, FactoryPending, FactoryDecided };

  void showCode(const std::string& flowId, const std::string& code);
  void showPending(const std::string& flowId, const std::string& accountName);
  void showDecided(bool approved);
  void showLocalSetup(const std::string& setupId, const std::string& code);
  void showFactoryPending(const std::string& resetId);
  void showFactoryDecided(bool approved);
  void hide();
  void dismissInformationalBanner();
  void decide(bool approved);
  static void dismissClicked(lv_event_t* event);
  static void approveClicked(lv_event_t* event);
  static void rejectClicked(lv_event_t* event);

  std::filesystem::path directory_;
  std::filesystem::path codePath_;
  std::filesystem::path pendingPath_;
  std::filesystem::path decisionPath_;
  std::filesystem::path localDirectory_;
  std::filesystem::path localSetupPath_;
  std::filesystem::path factoryPendingPath_;
  std::filesystem::path factoryDecisionPath_;
  std::chrono::steady_clock::time_point nextPoll_{};
  Mode mode_ = Mode::Hidden;
  std::string flowId_;
  std::string dismissedCodeFlowId_;
  std::string dismissedLocalSetupId_;
  lv_obj_t* modal_ = nullptr;
  lv_obj_t* title_ = nullptr;
  lv_obj_t* detail_ = nullptr;
  lv_obj_t* approve_ = nullptr;
  lv_obj_t* reject_ = nullptr;
};

} // namespace ardor
