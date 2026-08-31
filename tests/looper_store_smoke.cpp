#include "looper/LooperStore.h"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

constexpr std::size_t kBlockFrames = 4;

void require(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
}

void requireNear(float actual, float expected, const char* message)
{
  if (std::fabs(actual - expected) > 1.0e-5f) throw std::runtime_error(message);
}

void process(ardor::RealtimeLooper& looper, float leftValue, float rightValue)
{
  std::array<float, kBlockFrames> left;
  std::array<float, kBlockFrames> right;
  left.fill(leftValue);
  right.fill(rightValue);
  looper.processBlock(left.data(), right.data(), kBlockFrames);
}

void command(ardor::RealtimeLooper& looper, uint64_t& sequence,
             ardor::LooperCommandType type, float value = 0.0f)
{
  require(looper.tryEnqueue({sequence++, type, 0, value}), "failed to enqueue setup command");
}

ardor::LooperPausedSessionView makePausedLoop(ardor::RealtimeLooper& looper)
{
  std::string error;
  require(looper.prepare(48000.0f, kBlockFrames,
                         64 * ardor::RealtimeLooper::kBytesPerMasterFrame, error),
          "failed to prepare persistence fixture");
  uint64_t sequence = 1;
  command(looper, sequence, ardor::LooperCommandType::OpenEmpty);
  process(looper, 0.0f, 0.0f);
  command(looper, sequence, ardor::LooperCommandType::RecordOrOverdub);
  process(looper, 0.25f, 0.5f);
  process(looper, 0.25f, 0.5f);
  command(looper, sequence, ardor::LooperCommandType::RecordOrOverdub);
  process(looper, 0.0f, 0.0f);

  command(looper, sequence, ardor::LooperCommandType::RecordOrOverdub);
  process(looper, 0.1f, 0.2f); // wait for wrap
  process(looper, 0.1f, 0.2f);
  process(looper, 0.1f, 0.2f);
  command(looper, sequence, ardor::LooperCommandType::SetTrackLevelDb, -3.0f);
  process(looper, 0.0f, 0.0f);
  command(looper, sequence, ardor::LooperCommandType::ToggleTrackAudible);
  process(looper, 0.0f, 0.0f);
  command(looper, sequence, ardor::LooperCommandType::Pause);
  process(looper, 0.0f, 0.0f);
  const auto view = looper.pausedSessionView();
  require(view.has_value(), "paused looper should expose an immutable persistence view");
  require(view->loopFrames == 8 && view->tracks[0].present
            && view->tracks[0].lastTakeValid && view->tracks[0].lastTakeAudible
            && view->tracks[0].muted,
          "paused view should retain effective-take and mix metadata");
  return *view;
}

ardor::Preset sourcePreset()
{
  ardor::Preset preset;
  preset.name = "Persistence Tone";
  return preset;
}

nlohmann::json readJson(const std::filesystem::path& path)
{
  std::ifstream input(path);
  nlohmann::json json;
  input >> json;
  return json;
}

void writeJson(const std::filesystem::path& path, const nlohmann::json& json)
{
  std::ofstream output(path, std::ios::trunc);
  output << json.dump(2) << '\n';
}

} // namespace

int main()
{
  const auto unique = std::to_string(
    std::chrono::steady_clock::now().time_since_epoch().count());
  const auto root = std::filesystem::temp_directory_path() / ("ardor-looper-store-" + unique);
  try {
    ardor::RealtimeLooper looper;
    const auto paused = makePausedLoop(looper);
    ardor::LooperStore store(root);
    ardor::LooperSaveRequest request;
    request.name = "Night Sketch";
    request.sourcePreset = {3, 2, "Ambient Lead"};
    request.preset = sourcePreset();
    request.session = paused;
    std::string id;
    std::string error;
    require(store.save(request, id, error), "loop save should succeed");
    require(ardor::LooperStore::isValidId(id), "new loop should receive a safe hexadecimal id");

    ardor::LooperLoadedSet loaded;
    require(store.load(id, 64, loaded, error), "saved loop should load");
    require(loaded.name == "Night Sketch" && loaded.sourcePreset.bank == 3
              && loaded.sourcePreset.slot == 2 && loaded.preset.name == "Persistence Tone"
              && loaded.loopFrames == 8 && loaded.tracks[0].present
              && loaded.tracks[0].muted && loaded.tracks[0].levelDb == -3.0f,
            "manifest and preset snapshot should round trip");
    requireNear(loaded.tracks[0].audio.left[0], 0.35f,
                "save should flatten an audible latest take into left audio");
    requireNear(loaded.tracks[0].audio.right[0], 0.7f,
                "save should flatten an audible latest take into right audio");
    const auto loadedView = loaded.pausedSessionView();
    require(loadedView.loopFrames == loaded.loopFrames
              && loadedView.tracks[0].present
              && loadedView.tracks[0].baseLeft.data() == loaded.tracks[0].audio.left.data()
              && !loadedView.tracks[0].lastTakeValid,
            "loaded loop set should expose a zero-copy paused-session restore view");

    uint64_t sequence = 100;
    command(looper, sequence, ardor::LooperCommandType::ToggleUndo);
    process(looper, 0.0f, 0.0f);
    const auto undone = looper.pausedSessionView();
    require(undone && !undone->tracks[0].lastTakeAudible,
            "paused undo should be visible to persistence");
    request.id = id;
    request.session = *undone;
    std::string updatedId;
    require(store.save(request, updatedId, error) && updatedId == id,
            "saving an existing loop should publish a new revision under the same id");
    require(store.load(id, 64, loaded, error), "updated loop should load");
    requireNear(loaded.tracks[0].audio.left[0], 0.25f,
                "an undone latest take must be omitted from durable audio");
    requireNear(loaded.tracks[0].audio.right[0], 0.5f,
                "durable audio should contain the effective right channel");

    std::array<float, 8> nonFiniteLeft {};
    nonFiniteLeft[3] = std::numeric_limits<float>::quiet_NaN();
    auto invalidSession = *undone;
    invalidSession.tracks[0].baseLeft = nonFiniteLeft;
    request.session = invalidSession;
    require(!store.save(request, updatedId, error),
            "non-finite replacement revision should fail before manifest publication");
    require(store.load(id, 64, loaded, error)
              && std::fabs(loaded.tracks[0].audio.left[0] - 0.25f) < 1.0e-5f,
            "failed replacement must preserve the prior durable manifest");

    request.session = *undone;
    require(store.save(request, updatedId, error),
            "a later valid revision should recover after a failed publication");
    std::size_t managedFiles = 0;
    for (const auto& entry : std::filesystem::directory_iterator(store.pathFor(id))) {
      if (entry.is_regular_file()) ++managedFiles;
    }
    require(managedFiles == 3,
            "successful publication should clean orphan preset/audio revisions and temporaries");

    const auto entries = store.list(64);
    require(entries.size() == 1 && entries[0].available && entries[0].populatedTracks == 1,
            "library enumeration should validate and summarize saved loop sets");
    const auto unavailablePreset = store.list(
      64, [](const ardor::Preset&, std::string& validationError) {
        validationError = "source preset asset is missing";
        return false;
      });
    require(unavailablePreset.size() == 1 && !unavailablePreset[0].available
              && unavailablePreset[0].name == "Night Sketch"
              && unavailablePreset[0].unavailableReason == "source preset asset is missing",
            "library preset validation should disable load while retaining row metadata");

    const auto manifestPath = store.pathFor(id) / "manifest.json";
    const auto validManifest = readJson(manifestPath);
    auto traversalManifest = validManifest;
    traversalManifest["tracks"][0]["audioFile"] = "../escape.wav";
    writeJson(manifestPath, traversalManifest);
    require(!store.load(id, 64, loaded, error), "manifest traversal must be rejected");
    const auto unavailable = store.list(64);
    require(unavailable.size() == 1 && !unavailable[0].available
              && !unavailable[0].unavailableReason.empty(),
            "invalid library rows should retain a visible reason");
    writeJson(manifestPath, validManifest);

    std::filesystem::create_directories(root / "sentinel");
    require(!store.remove("../sentinel", error)
              && std::filesystem::exists(root / "sentinel"),
            "delete must never escape a validated hexadecimal loop id");
    require(store.remove(id, error) && !std::filesystem::exists(store.pathFor(id)),
            "validated loop deletion should remove exactly one loop directory");

    const std::string symlinkId(32, 'a');
    std::filesystem::create_directories(root / "outside");
    std::filesystem::create_directory_symlink(root / "outside", root / "loops" / symlinkId);
    require(!store.remove(symlinkId, error) && std::filesystem::exists(root / "outside"),
            "loop deletion should reject a symlink even when its target is beneath the data root");

    std::filesystem::remove_all(root);
    return 0;
  } catch (const std::exception& exception) {
    std::filesystem::remove_all(root);
    std::cerr << "looper_store_smoke failed: " << exception.what() << '\n';
    return 1;
  }
}
