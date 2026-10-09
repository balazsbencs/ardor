#pragma once

#include "audio/EngineLoader.h"
#include "audio/MiniaudioBackend.h"
#include "desktop/DesktopLibrary.h"
#include "dsp/Tuner.h"
#include "ui/UiModel.h"

#include <future>
#include <memory>

namespace ardor {

// Main/control-thread session. Engine preparation runs independently of the
// window and device; the audio callback never loads files or touches UiState.
class DesktopSession {
public:
  explicit DesktopSession(std::filesystem::path root);
  ~DesktopSession();

  UiState& state() { return state_; }
  const DesktopSettings& settings() const { return settings_; }
  const std::filesystem::path& dataRoot() const { return root_; }
  PedalEngine* engine() { return engine_.get(); }
  bool busy() const { return preparation_.valid(); }
  bool playing() const { return playing_; }
  const std::string& audioMessage() const { return audioMessage_; }

  void tick();
  bool startAudio();
  void stopAudio();
  bool configureAudio(DesktopSettings settings, std::string& error);
  void selectPreset(UiNavigationTarget target);
  void resolveNavigation(UiNavigationDecision decision);
  bool savePreset();
  void setTuner(bool enabled);
  void selectScene(std::size_t index);
  bool updateParameter(const std::string& blockId, const std::string& key, float value);

private:
  enum class PreparationKind { Initial, Preview, Navigation, BufferChange };
  struct Prepared {
    std::unique_ptr<PedalEngine> engine;
    Preset preset;
    UiNavigationTarget target;
    PreparationKind kind;
    std::string error;
  };
  void prepare(Preset preset, UiNavigationTarget target, PreparationKind kind);
  void navigate(UiNavigationTarget target);
  void failPreparation(const Prepared& prepared, const std::string& error);
  void persistSelection();

  std::filesystem::path root_;
  PresetStore store_;
  DesktopSettings settings_;
  UiState state_;
  std::unique_ptr<PedalEngine> engine_;
  MiniaudioBackend backend_; // Stopped before any engine is destroyed.
  std::future<Prepared> preparation_;
  TunerAnalyzer tuner_;
  bool playing_ = false;
  bool tunerEnabled_ = false;
  std::string audioMessage_ = "Audio stopped. Select an interface in Audio setup.";
  std::chrono::steady_clock::time_point nextTelemetry_{};
  std::uint64_t sceneRequest_ = 0;
  std::uint64_t snapshotSerial_ = 0;
  std::uint32_t engineBlockSize_ = 0;
};

} // namespace ardor
