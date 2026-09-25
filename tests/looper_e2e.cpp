#include "control/LinuxInput.h"
#include "looper/LooperController.h"
#include "looper/RealtimeLooper.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <linux/input.h>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;
constexpr std::size_t kFrames = 64;
using Clock = ardor::LooperController::Clock;

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

void near(float actual, float expected, const std::string& message)
{
  require(std::fabs(actual - expected) < 0.02f,
          message + ": got " + std::to_string(actual)
          + ", expected " + std::to_string(expected));
}

class TemporaryInputTree {
public:
  TemporaryInputTree()
  {
    root_ = std::filesystem::temp_directory_path()
      / ("ardor-looper-e2e-" + std::to_string(::getpid()));
    std::filesystem::create_directories(root_ / "sys");
    std::filesystem::create_directories(root_ / "dev");
    add("event0", "Goodix Capacitive TouchScreen");
    add("event1", "rotary-encoder");
    add("event9", "gpio-keys");
  }

  ~TemporaryInputTree() { std::filesystem::remove_all(root_); }
  const std::filesystem::path& root() const { return root_; }

  void appendKey(int index, bool pressed)
  {
    std::ofstream file(root_ / "dev" / "event9", std::ios::binary | std::ios::app);
    input_event key{};
    key.type = EV_KEY;
    key.code = static_cast<decltype(key.code)>(KEY_F1 + index);
    key.value = pressed ? 1 : 0;
    file.write(reinterpret_cast<const char*>(&key), sizeof(key));
    input_event sync{};
    sync.type = EV_SYN;
    file.write(reinterpret_cast<const char*>(&sync), sizeof(sync));
  }

private:
  void add(const char* event, const char* name)
  {
    const auto sysPath = root_ / "sys" / event / "device";
    std::filesystem::create_directories(sysPath);
    std::ofstream(sysPath / "name") << name << '\n';
    std::ofstream(root_ / "dev" / event, std::ios::binary);
  }

  std::filesystem::path root_;
};

class Pedal {
public:
  Pedal()
  {
    const auto devices = ardor::discoverPedalControlDevices(tree_.root() / "sys",
                                                              tree_.root() / "dev");
    require(devices.size() == 2, "auto discovery must find keys and encoder, not touch");
    const auto footswitch = std::find(devices.begin(), devices.end(),
                                      tree_.root() / "dev" / "event9");
    require(footswitch != devices.end(),
            "footswitch node may enumerate after touch and encoder");
    std::string error;
    require(input_.open(*footswitch, error),
            "open discovered footswitch: " + error);
    require(looper_.prepare(48000.0f, kFrames,
                            64 * kFrames * ardor::RealtimeLooper::kBytesPerMasterFrame,
                            error), "prepare looper: " + error);

    // Enter exactly as a player can: hold the active preset switch on the
    // discovered evdev node, then submit the host's OpenEmpty command.
    ardor::FootswitchGesture presetGestures;
    presetGestures.setLooperEntrySlot(0);
    tree_.appendKey(0, true);
    ardor::ControlEvent entryPress;
    require(input_.poll(entryPress)
              && !presetGestures.handle(entryPress, Clock::time_point{}),
            "active preset downstroke must wait for its looper hold");
    const auto entry = presetGestures.poll(Clock::time_point{} + 1000ms);
    require(entry && entry->type == ardor::FootswitchActionType::OpenLooper,
            "physical active-preset hold must request Looper mode");
    tree_.appendKey(0, false);
    ardor::ControlEvent entryRelease;
    require(input_.poll(entryRelease)
              && !presetGestures.handle(entryRelease, Clock::time_point{} + 1030ms),
            "entry release must not select another preset");
    const auto open = controller_.openSession();
    require(open && looper_.tryEnqueue(open->command), "open session through control queue");
    block(0.0f);
    require(telemetry_.sessionState == ardor::LooperSessionState::EmptyPaused,
            "audio thread must acknowledge open before switches are used");
  }

  void switchEvent(int index, bool pressed, std::chrono::milliseconds when)
  {
    tree_.appendKey(index, pressed);
    ardor::ControlEvent event;
    require(input_.poll(event), "evdev event must reach the pedal input reader");
    require(event.index == index
              && event.type == (pressed ? ardor::ControlEventType::FootswitchPressed
                                        : ardor::ControlEventType::FootswitchReleased),
            "evdev must preserve switch number and edge");
    submit(controller_.handleFootswitch(event, Clock::time_point{} + when));
  }

  void tap(int index, std::chrono::milliseconds when)
  {
    switchEvent(index, true, when);
    switchEvent(index, false, when + 30ms);
  }

  void poll(std::chrono::milliseconds when)
  {
    submit(controller_.poll(Clock::time_point{} + when));
  }

  float block(float live)
  {
    std::array<float, kFrames> left;
    std::array<float, kFrames> right;
    left.fill(live);
    right.fill(live);
    looper_.processBlock(left.data(), right.data(), kFrames);
    ardor::LooperTelemetry next;
    while (looper_.tryReadTelemetry(next)) {
      telemetry_ = next;
      controller_.updateTelemetry(next);
    }
    near(left.front(), right.front(), "stereo output must remain symmetric");
    return left[kFrames / 2];
  }

  void blocks(int count, float live)
  {
    for (int n = 0; n < count; ++n) block(live);
  }

  void request(ardor::LooperCommandType type)
  {
    submit(controller_.requestCommand(type));
  }

  void close()
  {
    submit(controller_.closeSession());
  }

  ardor::LooperTelemetry state() const { return telemetry_; }
  std::size_t selectedTrack() const { return controller_.selectedTrack(); }
  bool locked() const { return controller_.sessionLocked(); }
  bool tunerActive() const { return controller_.tunerActive(); }

private:
  void submit(const std::optional<ardor::LooperControllerAction>& action)
  {
    if (!action) return;
    if (action->type == ardor::LooperControllerActionType::EnterTuner) return;
    require(looper_.tryEnqueue(action->command), "footswitch command must reach audio thread");
  }

  TemporaryInputTree tree_;
  ardor::LinuxInputDevice input_;
  ardor::LooperController controller_;
  ardor::RealtimeLooper looper_;
  ardor::LooperTelemetry telemetry_;
};

void testPhysicalSwitchToAudio()
{
  Pedal pedal;
  pedal.tap(2, 0ms); // FS3: record first loop
  pedal.blocks(5, 0.25f);
  require(pedal.state().sessionState == ardor::LooperSessionState::RecordingMaster,
          "FS3 must start the master recording");
  pedal.tap(2, 200ms); // FS3: close loop
  near(pedal.block(0.0f), 0.25f, "first recording must play back");
  require(pedal.state().masterFrames == 5 * kFrames
            && pedal.state().sessionState == ardor::LooperSessionState::Running,
          "second FS3 must establish the shared loop length");

  pedal.tap(3, 300ms); // FS4: mute
  near(pedal.block(0.0f), 0.0f, "FS4 must mute selected track");
  require(pedal.state().tracks[0].state == ardor::LooperTrackState::Muted,
          "mute state must be visible in telemetry");
  pedal.tap(3, 350ms); // FS4: unmute
  near(pedal.block(0.0f), 0.25f, "FS4 must restore playback at the shared phase");

  pedal.tap(1, 400ms); // FS2: select track 2
  require(pedal.selectedTrack() == 1, "FS2 must select the next track");
  pedal.tap(2, 450ms); // FS3: arm follower track
  pedal.block(0.5f);
  require(pedal.state().tracks[1].state == ardor::LooperTrackState::ArmedRecord,
          "FS3 on an empty follower must arm until the next loop boundary");
  pedal.blocks(15, 0.5f);
  require(pedal.state().tracks[1].state == ardor::LooperTrackState::Playing,
          "follower must finish after exactly one master cycle");
  near(pedal.block(0.0f), 0.75f, "master and follower must mix during playback");

  pedal.tap(2, 500ms); // FS3: arm overdub on track 2
  pedal.block(0.125f);
  require(pedal.state().tracks[1].state == ardor::LooperTrackState::ArmedOverdub
            || pedal.state().tracks[1].state == ardor::LooperTrackState::Overdubbing,
          "FS3 on a populated track must arm or start an overdub at the boundary");
  pedal.blocks(15, 0.125f);
  require(pedal.state().tracks[1].state == ardor::LooperTrackState::Playing
            && pedal.state().tracks[1].undoAvailable,
          "overdub must complete and expose one-level undo");
  pedal.blocks(3, 0.0f);
  near(pedal.block(0.0f), 0.875f, "completed overdub must be audible");

  pedal.tap(0, 600ms); // FS1: undo selected track's last take
  near(pedal.block(0.0f), 0.75f, "FS1 must undo the selected track's overdub");
  require(pedal.state().tracks[1].undoApplied, "undo must be reported");
  pedal.tap(0, 650ms); // FS1: redo
  near(pedal.block(0.0f), 0.875f, "FS1 must redo the take");

  pedal.switchEvent(1, true, 700ms); // FS2 hold: clear selected track
  pedal.poll(2199ms);
  require(pedal.state().tracks[1].state == ardor::LooperTrackState::Playing,
          "clear must wait for the full hold");
  pedal.poll(2200ms);
  pedal.block(0.0f);
  require(pedal.state().tracks[1].state == ardor::LooperTrackState::Empty
            && pedal.state().masterFrames == 5 * kFrames,
          "FS2 hold must clear only selected track and retain master length");
  pedal.switchEvent(1, false, 2250ms);
  require(pedal.selectedTrack() == 1, "clear hold release must not select a new track");

  pedal.request(ardor::LooperCommandType::Pause);
  pedal.block(0.0f);
  require(pedal.state().sessionState == ardor::LooperSessionState::Paused,
          "Stop All must pause the shared playhead");
  pedal.request(ardor::LooperCommandType::Resume);
  near(pedal.block(0.0f), 0.25f, "Resume must restore master playback");

  pedal.switchEvent(0, true, 3000ms);
  pedal.switchEvent(1, true, 3050ms);
  pedal.poll(4049ms);
  require(!pedal.tunerActive(), "tuner chord must wait for the one-second hold");
  pedal.poll(4050ms);
  pedal.block(0.0f); // audio thread acknowledges Pause before output is muted
  require(pedal.state().sessionState == ardor::LooperSessionState::Paused,
          "tuner chord must pause playback first");
  pedal.poll(4060ms);
  require(pedal.tunerActive(), "tuner must open after pause acknowledgement");
  pedal.switchEvent(0, false, 4070ms);
  pedal.switchEvent(1, false, 4070ms);
  pedal.switchEvent(3, true, 4100ms); // any switch exits tuner
  pedal.switchEvent(3, false, 4130ms);
  near(pedal.block(0.0f), 0.25f, "tuner exit must resume a previously running loop");
  require(!pedal.tunerActive(), "tuner exit must restore looper controls");

  pedal.tap(1, 4200ms);
  pedal.tap(1, 4240ms);
  pedal.tap(1, 4280ms); // wrap Track 2 through 3 and 4 to Track 1
  require(pedal.selectedTrack() == 0, "FS2 must wrap selection to the master track");
  pedal.switchEvent(1, true, 4340ms);
  pedal.poll(5840ms);
  pedal.block(0.0f);
  require(pedal.state().sessionState == ardor::LooperSessionState::EmptyPaused
            && pedal.state().masterFrames == 0,
          "clearing the last populated track must reset the master length");
  pedal.switchEvent(1, false, 5890ms);
  pedal.close();
  pedal.block(0.0f);
  require(!pedal.locked()
            && pedal.state().sessionState == ardor::LooperSessionState::Inactive,
          "closing an empty session must release the preset lock");
}

} // namespace

int main()
{
  try {
    testPhysicalSwitchToAudio();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "looper_e2e failed: " << error.what() << '\n';
    return 1;
  }
}
