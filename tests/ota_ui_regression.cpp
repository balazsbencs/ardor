#include "ui/LvglUi.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

lv_obj_t* findLabel(lv_obj_t* root, const std::string& text)
{
  if (lv_obj_check_type(root, &lv_label_class) && text == lv_label_get_text(root)) return root;
  for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
    if (auto* found = findLabel(lv_obj_get_child(root, i), text)) return found;
  }
  return nullptr;
}

int main()
{
  lv_init();
  auto* display = lv_display_create(1280, 720);
  auto state = ardor::makeDemoUiState();
  std::atomic<int> reads{0}, checks{0}, installs{0};
  ardor::UiActions actions;
  actions.readUpdateStatus = [&](ardor::DeviceUpdateStatus& status, std::string& error) {
    const int read = ++reads;
    std::this_thread::sleep_for(250ms);
    if (read == 2) { error = "Manager restarting"; return false; }
    status.enabled = true;
    status.installedVersion = "1.2.3";
    status.state = installs ? "failed" : "idle";
    status.errorMessage = installs ? "Simulated download failure" : "";
    return true;
  };
  actions.checkForUpdate = [&](ardor::DeviceUpdateStatus& status, std::string&) {
    ++checks;
    std::this_thread::sleep_for(250ms);
    status.state = "available";
    status.availableVersion = "1.3.0";
    return true;
  };
  actions.installUpdate = [&](const std::string&, ardor::DeviceUpdateStatus& status, std::string&) {
    ++installs;
    std::this_thread::sleep_for(250ms);
    status.state = "downloading";
    return true;
  };
  ardor::LvglUi ui(actions);
  ui.build(lv_screen_active(), state);
  const auto timed = [&](auto action, const char* name) {
    auto start = std::chrono::steady_clock::now();
    action();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    std::cout << name << ": " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << "ms\n";
    return elapsed < 100ms;
  };
  const auto pump = [&](auto done) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
      ui.refresh(lv_screen_active(), state);
      if (done()) return true;
      std::this_thread::sleep_for(5ms);
    }
    return false;
  };
  if (!timed([&]{ui.openSettings(state);}, "open settings")) return 1;
  ui.showSettingsSection(state, 4);
  if (!pump([&]{return findLabel(lv_screen_active(), "1.2.3");})) return 5;
  if (!timed([&]{ui.checkForUpdate(state);}, "check update")) return 1;
  ui.checkForUpdate(state); // A second press must not create another worker.
  if (!pump([&]{return findLabel(lv_screen_active(), "INSTALL & RESTART");})) return 6;
  ui.installUpdate(state); // Arm only.
  if (installs != 0) return 2;
  if (!timed([&]{ui.installUpdate(state);}, "confirm install")) return 1;
  if (!pump([&]{return findLabel(lv_screen_active(), "SIMULATED DOWNLOAD FAILURE");})) return 3;
  if (checks != 1 || installs != 1) return 4;
  // Navigating away while a read is in flight must remain safe.
  ui.openSettings(state);
  ui.closeSettings(state);
  ui.refresh(lv_screen_active(), state);
  lv_display_delete(display);
  std::cout << "OTA UI remains responsive and polls the installation\n";
}
