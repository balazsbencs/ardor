#include "looper/LooperStore.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ardor {
namespace {

constexpr int kManifestVersion = 1;

std::string randomHex(std::size_t digits)
{
  std::random_device random;
  constexpr char hex[] = "0123456789abcdef";
  std::string result(digits, '0');
  for (char& character : result) character = hex[random() & 0x0fU];
  return result;
}

std::string isoUtcNow()
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm utc {};
#ifdef _WIN32
  gmtime_s(&utc, &seconds);
#else
  gmtime_r(&seconds, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

bool allHex(std::string_view value)
{
  return !value.empty() && std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= '0' && character <= '9')
        || (character >= 'a' && character <= 'f');
  });
}

bool validRevisionFile(std::string_view file, std::string_view prefix, std::string_view suffix)
{
  if (!file.starts_with(prefix) || !file.ends_with(suffix)
      || file.size() <= prefix.size() + suffix.size()) return false;
  const auto revision = file.substr(prefix.size(), file.size() - prefix.size() - suffix.size());
  return revision.size() >= 8 && revision.size() <= 32 && allHex(revision);
}

bool pathStartsWith(const std::filesystem::path& path, const std::filesystem::path& base)
{
  auto pathPart = path.begin();
  for (auto basePart = base.begin(); basePart != base.end(); ++basePart, ++pathPart) {
    if (pathPart == path.end() || *pathPart != *basePart) return false;
  }
  return true;
}

bool containedExistingFile(const std::filesystem::path& directory,
                           const std::filesystem::path& file,
                           std::string& error)
{
  std::error_code filesystemError;
  const auto canonicalDirectory = std::filesystem::canonical(directory, filesystemError);
  if (filesystemError) {
    error = "failed to resolve loop directory: " + filesystemError.message();
    return false;
  }
  const auto canonicalFile = std::filesystem::canonical(file, filesystemError);
  if (filesystemError || !pathStartsWith(canonicalFile, canonicalDirectory)) {
    error = filesystemError ? "failed to resolve loop file: " + filesystemError.message()
                            : "loop manifest path escapes its loop directory";
    return false;
  }
  return true;
}

bool validateLibraryDirectory(const std::filesystem::path& root,
                              const std::filesystem::path& directory,
                              bool create,
                              std::string& error)
{
  std::error_code filesystemError;
  if (create) {
    std::filesystem::create_directories(root / "loops", filesystemError);
    if (!filesystemError) std::filesystem::create_directories(directory, filesystemError);
  }
  if (filesystemError) {
    error = "failed to create loop directory: " + filesystemError.message();
    return false;
  }
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(root / "loops", filesystemError))
      || std::filesystem::is_symlink(std::filesystem::symlink_status(directory, filesystemError))) {
    error = "symbolic links are not allowed in the loop library";
    return false;
  }
  const auto canonicalRoot = std::filesystem::canonical(root, filesystemError);
  if (filesystemError) {
    error = "failed to resolve data root: " + filesystemError.message();
    return false;
  }
  const auto canonicalLoops = std::filesystem::canonical(root / "loops", filesystemError);
  if (filesystemError || canonicalLoops.parent_path() != canonicalRoot) {
    error = "loop library escapes the data root";
    return false;
  }
  const auto canonicalDirectory = std::filesystem::canonical(directory, filesystemError);
  if (filesystemError || canonicalDirectory.parent_path() != canonicalLoops) {
    error = "loop path escapes the loop library";
    return false;
  }
  return true;
}

#ifndef _WIN32
void syncPath(const std::filesystem::path& path)
{
  const int descriptor = ::open(path.string().c_str(), O_RDONLY);
  if (descriptor < 0) throw std::runtime_error("failed to open for sync: " + path.string());
  if (::fsync(descriptor) != 0) {
    ::close(descriptor);
    throw std::runtime_error("failed to sync: " + path.string());
  }
  ::close(descriptor);
}
#else
void syncPath(const std::filesystem::path&) {}
#endif

void writeJsonFile(const std::filesystem::path& path, const nlohmann::json& json)
{
  std::ofstream output(path, std::ios::trunc);
  if (!output) throw std::runtime_error("failed to create: " + path.string());
  output << json.dump(2) << '\n';
  output.flush();
  if (!output.good()) throw std::runtime_error("failed to write: " + path.string());
  output.close();
  if (output.fail()) throw std::runtime_error("failed to close: " + path.string());
  syncPath(path);
}

nlohmann::json readJsonFile(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input) throw std::runtime_error("failed to open: " + path.string());
  nlohmann::json json;
  input >> json;
  return json;
}

std::string defaultLoopName()
{
  std::string timestamp = isoUtcNow();
  std::replace(timestamp.begin(), timestamp.end(), 'T', ' ');
  if (!timestamp.empty() && timestamp.back() == 'Z') timestamp.pop_back();
  return "Loop " + timestamp;
}

void cleanupUnreferenced(const std::filesystem::path& directory,
                         const std::set<std::string>& retained)
{
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
    if (error || !entry.is_regular_file(error)) continue;
    const auto name = entry.path().filename().string();
    const bool managed = validRevisionFile(name, "preset-", ".json")
      || validRevisionFile(name, "track-1-", ".wav")
      || validRevisionFile(name, "track-2-", ".wav")
      || validRevisionFile(name, "track-3-", ".wav")
      || validRevisionFile(name, "track-4-", ".wav")
      || name.ends_with(".tmp");
    if (managed && !retained.contains(name)) std::filesystem::remove(entry.path(), error);
  }
}

} // namespace

LooperStore::LooperStore(std::filesystem::path dataRoot)
  : root_(std::move(dataRoot))
{
}

LooperPausedSessionView LooperLoadedSet::pausedSessionView() const noexcept
{
  LooperPausedSessionView view;
  view.sampleRate = static_cast<float>(sampleRate);
  view.loopFrames = loopFrames;
  for (std::size_t index = 0; index < tracks.size(); ++index) {
    const auto& source = tracks[index];
    auto& destination = view.tracks[index];
    destination.present = source.present;
    destination.muted = source.muted;
    destination.levelDb = source.levelDb;
    destination.balance = source.balance;
    if (!source.present) continue;
    destination.baseLeft = source.audio.left;
    destination.baseRight = source.audio.right;
  }
  return view;
}

bool LooperStore::isValidId(std::string_view id) noexcept
{
  return id.size() == 32 && allHex(id);
}

std::filesystem::path LooperStore::pathFor(std::string_view id) const
{
  if (!isValidId(id)) throw std::invalid_argument("invalid loop id");
  return root_ / "loops" / std::string(id);
}

bool LooperStore::save(const LooperSaveRequest& request,
                       std::string& savedId,
                       std::string& error) const
{
  error.clear();
  savedId.clear();
  if (request.session.loopFrames == 0 || request.session.sampleRate != 48000.0f) {
    error = "only a non-empty paused 48 kHz loop can be saved";
    return false;
  }
  if (request.sourcePreset.bank < 0 || request.sourcePreset.bank >= 100
      || request.sourcePreset.slot < 0 || request.sourcePreset.slot >= 4) {
    error = "source preset location is invalid";
    return false;
  }
  const std::string id = request.id.empty() ? randomHex(32) : request.id;
  if (!isValidId(id)) {
    error = "invalid loop id";
    return false;
  }
  const auto directory = pathFor(id);
  const auto revision = randomHex(12);
  const auto presetName = "preset-" + revision + ".json";
  const auto presetTemporary = directory / (presetName + ".tmp");
  std::vector<std::filesystem::path> temporaryFiles;
  std::set<std::string> retained {"manifest.json", presetName};
  try {
    if (!validateLibraryDirectory(root_, directory, true, error)) return false;
    std::error_code filesystemError;
    if (!std::filesystem::is_directory(directory, filesystemError)) {
      throw std::runtime_error("loop path is not a directory");
    }
    temporaryFiles.push_back(presetTemporary);
    std::filesystem::remove(presetTemporary, filesystemError);
    filesystemError.clear();
    writeJsonFile(presetTemporary, toJson(request.preset));
    std::filesystem::rename(presetTemporary, directory / presetName);

    nlohmann::json tracks = nlohmann::json::array();
    std::size_t populatedTracks = 0;
    for (std::size_t index = 0; index < request.session.tracks.size(); ++index) {
      const auto& track = request.session.tracks[index];
      nlohmann::json trackJson = {
        {"index", index}, {"present", track.present}, {"levelDb", track.levelDb},
        {"balance", track.balance}, {"muted", track.muted},
      };
      if (!std::isfinite(track.levelDb) || track.levelDb < -60.0f || track.levelDb > 6.0f
          || !std::isfinite(track.balance) || track.balance < -1.0f || track.balance > 1.0f) {
        throw std::runtime_error("track mix setting is outside the supported range");
      }
      if (track.present) {
        ++populatedTracks;
        const auto audioName = "track-" + std::to_string(index + 1) + "-" + revision + ".wav";
        const auto temporary = directory / (audioName + ".tmp");
        temporaryFiles.push_back(temporary);
        std::filesystem::remove(temporary, filesystemError);
        filesystemError.clear();
        std::string wavError;
        if (!writeLooperFloatWav(temporary, track, request.session.loopFrames, 48000, wavError)) {
          throw std::runtime_error(wavError);
        }
        syncPath(temporary);
        std::filesystem::rename(temporary, directory / audioName);
        trackJson["audioFile"] = audioName;
        retained.insert(audioName);
      }
      tracks.push_back(std::move(trackJson));
    }
    if (populatedTracks == 0) throw std::runtime_error("loop has no populated tracks");

    const std::string savedAt = isoUtcNow();
    const nlohmann::json manifest = {
      {"version", kManifestVersion}, {"id", id},
      {"name", request.name.empty() ? defaultLoopName() : request.name},
      {"savedAt", savedAt}, {"sampleRate", 48000}, {"channels", 2},
      {"loopFrames", request.session.loopFrames}, {"presetFile", presetName},
      {"sourcePreset", {{"bank", request.sourcePreset.bank},
                         {"slot", request.sourcePreset.slot},
                         {"name", request.sourcePreset.name}}},
      {"tracks", std::move(tracks)},
    };
    const auto manifestTemporary = directory / "manifest.json.tmp";
    temporaryFiles.push_back(manifestTemporary);
    std::error_code removeError;
    std::filesystem::remove(manifestTemporary, removeError);
    writeJsonFile(manifestTemporary, manifest);
    std::filesystem::rename(manifestTemporary, directory / "manifest.json");
    syncPath(directory);
    cleanupUnreferenced(directory, retained);
    savedId = id;
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    std::error_code ignored;
    for (const auto& temporary : temporaryFiles) std::filesystem::remove(temporary, ignored);
    return false;
  }
}

bool LooperStore::load(std::string_view id, uint64_t maximumFrames,
                       LooperLoadedSet& loopSet, std::string& error) const
{
  error.clear();
  loopSet = {};
  if (!isValidId(id)) {
    error = "invalid loop id";
    return false;
  }
  const auto directory = pathFor(id);
  const auto manifestPath = directory / "manifest.json";
  try {
    if (!validateLibraryDirectory(root_, directory, false, error)) return false;
    if (!containedExistingFile(directory, manifestPath, error)) return false;
    const auto manifest = readJsonFile(manifestPath);
    if (manifest.at("version").get<int>() != kManifestVersion) {
      throw std::runtime_error("unsupported loop manifest version");
    }
    loopSet.id = manifest.at("id").get<std::string>();
    if (loopSet.id != id || !isValidId(loopSet.id)) throw std::runtime_error("loop id mismatch");
    loopSet.name = manifest.at("name").get<std::string>();
    loopSet.savedAt = manifest.at("savedAt").get<std::string>();
    loopSet.sampleRate = manifest.at("sampleRate").get<uint32_t>();
    const auto channels = manifest.at("channels").get<uint32_t>();
    loopSet.loopFrames = manifest.at("loopFrames").get<uint64_t>();
    if (loopSet.sampleRate != 48000 || channels != 2 || loopSet.loopFrames == 0
        || loopSet.loopFrames > maximumFrames) {
      throw std::runtime_error("loop format exceeds the prepared 48 kHz stereo memory tier");
    }
    const auto& source = manifest.at("sourcePreset");
    loopSet.sourcePreset.bank = source.at("bank").get<int>();
    loopSet.sourcePreset.slot = source.at("slot").get<int>();
    loopSet.sourcePreset.name = source.at("name").get<std::string>();
    if (loopSet.sourcePreset.bank < 0 || loopSet.sourcePreset.bank >= 100
        || loopSet.sourcePreset.slot < 0 || loopSet.sourcePreset.slot >= 4) {
      throw std::runtime_error("source preset location is invalid");
    }
    const auto presetFile = manifest.at("presetFile").get<std::string>();
    if (!validRevisionFile(presetFile, "preset-", ".json")) {
      throw std::runtime_error("invalid preset snapshot filename");
    }
    const auto presetPath = directory / presetFile;
    if (!containedExistingFile(directory, presetPath, error)) return false;
    loopSet.preset = presetFromJson(readJsonFile(presetPath));

    const auto& tracks = manifest.at("tracks");
    if (!tracks.is_array() || tracks.size() != kLooperTrackCount) {
      throw std::runtime_error("loop manifest must describe exactly four tracks");
    }
    std::array<bool, kLooperTrackCount> seen {};
    std::size_t populatedTracks = 0;
    for (const auto& trackJson : tracks) {
      const auto index = trackJson.at("index").get<std::size_t>();
      if (index >= kLooperTrackCount || seen[index]) throw std::runtime_error("duplicate or invalid track index");
      seen[index] = true;
      auto& track = loopSet.tracks[index];
      track.present = trackJson.at("present").get<bool>();
      track.levelDb = trackJson.value("levelDb", 0.0f);
      track.balance = trackJson.value("balance", 0.0f);
      track.muted = trackJson.value("muted", false);
      if (!std::isfinite(track.levelDb) || track.levelDb < -60.0f || track.levelDb > 6.0f
          || !std::isfinite(track.balance) || track.balance < -1.0f || track.balance > 1.0f) {
        throw std::runtime_error("track mix setting is outside the supported range");
      }
      if (!track.present) continue;
      ++populatedTracks;
      const auto audioFile = trackJson.at("audioFile").get<std::string>();
      const auto expectedPrefix = "track-" + std::to_string(index + 1) + "-";
      if (!validRevisionFile(audioFile, expectedPrefix, ".wav")) {
        throw std::runtime_error("invalid loop audio filename");
      }
      const auto audioPath = directory / audioFile;
      if (!containedExistingFile(directory, audioPath, error)) return false;
      std::string wavError;
      if (!readLooperFloatWav(audioPath, maximumFrames, track.audio, wavError)) {
        throw std::runtime_error(wavError);
      }
      if (track.audio.left.size() != loopSet.loopFrames) {
        throw std::runtime_error("track length differs from the master loop");
      }
    }
    if (populatedTracks == 0) throw std::runtime_error("loop has no populated tracks");
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    loopSet = {};
    return false;
  }
}

std::vector<LooperLibraryEntry> LooperStore::list(
    uint64_t maximumFrames, const PresetValidator& validatePreset) const
{
  std::vector<LooperLibraryEntry> entries;
  std::error_code error;
  const auto loops = root_ / "loops";
  for (const auto& directory : std::filesystem::directory_iterator(loops, error)) {
    if (error || !directory.is_directory(error)) continue;
    const auto id = directory.path().filename().string();
    if (!isValidId(id)) continue;
    LooperLoadedSet loaded;
    std::string loadError;
    LooperLibraryEntry entry;
    entry.id = id;
    entry.available = load(id, maximumFrames, loaded, loadError);
    if (entry.available && validatePreset
        && !validatePreset(loaded.preset, loadError)) {
      entry.available = false;
    }
    if (entry.available) {
      entry.name = loaded.name;
      entry.savedAt = loaded.savedAt;
      entry.sourcePresetName = loaded.sourcePreset.name;
      entry.loopFrames = loaded.loopFrames;
      entry.populatedTracks = static_cast<std::size_t>(std::count_if(
        loaded.tracks.begin(), loaded.tracks.end(), [](const auto& track) { return track.present; }));
    } else {
      entry.name = loaded.name.empty() ? id : loaded.name;
      entry.savedAt = loaded.savedAt;
      entry.sourcePresetName = loaded.sourcePreset.name;
      entry.loopFrames = loaded.loopFrames;
      entry.populatedTracks = static_cast<std::size_t>(std::count_if(
        loaded.tracks.begin(), loaded.tracks.end(), [](const auto& track) { return track.present; }));
      entry.unavailableReason = std::move(loadError);
    }
    entries.push_back(std::move(entry));
  }
  std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
    return left.savedAt > right.savedAt;
  });
  return entries;
}

bool LooperStore::remove(std::string_view id, std::string& error) const
{
  error.clear();
  if (!isValidId(id)) {
    error = "invalid loop id";
    return false;
  }
  const auto directory = pathFor(id);
  std::error_code filesystemError;
  if (!std::filesystem::exists(directory, filesystemError)) {
    error = filesystemError ? filesystemError.message() : "loop set does not exist";
    return false;
  }
  if (!validateLibraryDirectory(root_, directory, false, error)) return false;
  const auto canonicalLoops = std::filesystem::canonical(root_ / "loops", filesystemError);
  const auto canonicalDirectory = std::filesystem::canonical(directory, filesystemError);
  if (filesystemError || canonicalDirectory.parent_path() != canonicalLoops) {
    error = "loop path escapes the loop library";
    return false;
  }
  std::filesystem::remove_all(canonicalDirectory, filesystemError);
  if (filesystemError) {
    error = "failed to delete loop set: " + filesystemError.message();
    return false;
  }
  return true;
}

} // namespace ardor
