#include <clap/clap.h>
#include "preset/Preset.h"
#include "preset/PresetStore.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "ClapTestPlatform.h"
#include <filesystem>
#include <iostream>
#include <new>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
thread_local bool inProcess = false;
std::atomic<std::size_t> processAllocations{0};
}
void* operator new(std::size_t size) {
  if (inProcess) processAllocations.fetch_add(1);
  if (auto* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Host {
  bool callback = false, restart = false;
  int dirty = 0;
  std::vector<std::string> errors;
  clap_host_t api{CLAP_VERSION, this, "Ardor ABI smoke host", "Ardor", "", "1", extension,
                 requestRestart, requestProcess, requestCallback};
  static Host& self(const clap_host_t* h) { return *static_cast<Host*>(h->host_data); }
  static void CLAP_ABI requestRestart(const clap_host_t* h) { self(h).restart = true; }
  static void CLAP_ABI requestProcess(const clap_host_t*) {}
  static void CLAP_ABI requestCallback(const clap_host_t* h) { self(h).callback = true; }
  static void CLAP_ABI log(const clap_host_t* h, clap_log_severity, const char* message) { self(h).errors.emplace_back(message); }
  static void CLAP_ABI rescan(const clap_host_t*, clap_param_rescan_flags) {}
  static void CLAP_ABI clear(const clap_host_t*, clap_id, uint32_t) {}
  static void CLAP_ABI dirtyState(const clap_host_t* h) { ++self(h).dirty; }
  static const void* CLAP_ABI extension(const clap_host_t*, const char* id) {
    static const clap_host_log_t logging{log};
    static const clap_host_params_t params{rescan, clear, requestProcess};
    static const clap_host_state_t state{dirtyState};
    if (!std::strcmp(id, CLAP_EXT_LOG)) return &logging;
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
    if (!std::strcmp(id, CLAP_EXT_STATE)) return &state;
    return nullptr;
  }
};
struct Events {
  std::vector<clap_event_param_value_t> values;
  clap_input_events_t api{this, size, get};
  void add(clap_id id, double value, uint32_t time = 0) {
    clap_event_param_value_t e{};
    e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
    e.param_id = id; e.value = value;
    e.note_id = e.port_index = e.channel = e.key = -1;
    values.push_back(e);
  }
  static uint32_t CLAP_ABI size(const clap_input_events_t* e) { return static_cast<Events*>(e->ctx)->values.size(); }
  static const clap_event_header_t* CLAP_ABI get(const clap_input_events_t* e, uint32_t i) {
    const auto& values = static_cast<Events*>(e->ctx)->values;
    return i < values.size() ? &values[i].header : nullptr;
  }
};
struct Stream {
  std::string bytes;
  std::size_t position = 0;
  clap_ostream_t out{this, write};
  clap_istream_t in{this, read};
  static int64_t CLAP_ABI write(const clap_ostream_t* s, const void* data, uint64_t size) {
    const auto n = std::min<uint64_t>(size, 7); // exercise short writes
    static_cast<Stream*>(s->ctx)->bytes.append(static_cast<const char*>(data), n); return n;
  }
  static int64_t CLAP_ABI read(const clap_istream_t* s, void* data, uint64_t size) {
    auto& stream = *static_cast<Stream*>(s->ctx);
    const auto n = std::min<std::size_t>({static_cast<std::size_t>(size), 11, stream.bytes.size() - stream.position});
    std::memcpy(data, stream.bytes.data() + stream.position, n); stream.position += n; return n;
  }
};
struct Audio {
  std::vector<float> l, r, outL, outR;
  std::array<float*, 2> inputs, outputs;
  clap_audio_buffer_t in{}, out{};
  clap_process_t block{};
  Audio(std::size_t size) : l(size), r(size), outL(size), outR(size), inputs{l.data(), r.data()}, outputs{outL.data(), outR.data()} {
    in.data32 = inputs.data(); in.channel_count = 2;
    out.data32 = outputs.data(); out.channel_count = 2;
    block.frames_count = size; block.audio_inputs = &in; block.audio_outputs = &out;
    block.audio_inputs_count = block.audio_outputs_count = 1;
  }
  void run(const clap_plugin_t* plugin, Events* events = nullptr) {
    block.in_events = events ? &events->api : nullptr;
    const auto before = processAllocations.load();
    inProcess = true;
    const auto result = plugin->process(plugin, &block);
    inProcess = false;
    require(result != CLAP_PROCESS_ERROR, "CLAP process failed");
    require(processAllocations.load() == before, "Audio callback allocated C++ objects");
    for (std::size_t i = 0; i < l.size(); ++i) require(std::isfinite(outL[i]) && std::isfinite(outR[i]), "Non-finite plugin output");
  }
};
struct Plugin {
  const clap_plugin_t* api;
  bool active = false, started = false;
  Plugin(const clap_plugin_factory_t* factory, Host& host) : api(factory->create_plugin(factory, &host.api, "org.ardor.guitar")) {
    require(api && api->init(api), "Cannot create/init CLAP");
  }
  ~Plugin() { stop(); api->destroy(api); }
  void start() { require(api->activate(api, 48000, 1, 2048), "Cannot activate CLAP"); active = true; require(api->start_processing(api), "Cannot start CLAP"); started = true; }
  void stop() { if (started) api->stop_processing(api); if (active) api->deactivate(api); started = active = false; }
  template<class T> const T* ext(const char* id) { return static_cast<const T*>(api->get_extension(api, id)); }
};
}

int main() {
  const auto root = std::filesystem::temp_directory_path() / ("ardor-clap-smoke-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  // Preserve the environment so the smoke host never touches a real library.
#ifdef __APPLE__
  const char* env = "HOME";
  const auto library = root / "Library/Application Support/Ardor";
#elif defined(_WIN32)
  const char* env = "LOCALAPPDATA";
  const auto library = root / "Ardor";
#else
  const char* env = "XDG_DATA_HOME";
  const auto library = root / "ardor";
#endif
  const char* prior = std::getenv(env);
  const std::string saved = prior ? prior : "";
  clap_test::environment(env, root.string().c_str());
  void* handle = nullptr;
  try {
    handle = clap_test::open(ARDOR_CLAP_BINARY);
    require(handle, clap_test::error());
    const auto* entry = static_cast<const clap_plugin_entry_t*>(clap_test::symbol(handle, "clap_entry"));
    require(entry && entry->init(ARDOR_CLAP_BINARY), "Missing CLAP entry");
    const auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    require(factory && factory->get_plugin_count(factory) == 1, "Missing factory");
    require(!factory->get_plugin_descriptor(factory, 1), "Out-of-range descriptor accepted");
    Host host, otherHost;
    {
      Plugin a(factory, host), b(factory, otherHost);
      require(!std::filesystem::exists(library), "Plugin rewrote the user's desktop library on init");
      const auto* params = a.ext<clap_plugin_params_t>(CLAP_EXT_PARAMS);
      const auto* state = a.ext<clap_plugin_state_t>(CLAP_EXT_STATE);
      require(params && state && params->count(a.api) == 6, "Missing parameters/state");
      for (uint32_t i = 0; i < params->count(a.api); ++i) {
        clap_param_info_t info{};
        require(params->get_info(a.api, i, &info), "Missing parameter info");
        char text[128]; double value = 0;
        require(params->value_to_text(a.api, i, info.default_value, text, sizeof(text)) && params->text_to_value(a.api, i, text, &value), "Parameter text round-trip failed");
        require(value == info.default_value, "Parameter text changed the default");
      }
      require(!a.api->activate(a.api, 44100, 1, 2048) && !a.api->activate(a.api, 96000, 1, 2048), "Unsupported rate silently ran");
      a.start(); b.start();
      const auto* latency = a.ext<clap_plugin_latency_t>(CLAP_EXT_LATENCY);
      require(latency && latency->get(a.api) == 64, "Wrong quantum latency");
      std::vector<float> rendered;
      const std::array<int, 9> sizes{1, 7, 31, 63, 64, 65, 127, 511, 2048};
      std::size_t position = 0;
      for (int size : sizes) {
        Audio audio(size);
        for (int i = 0; i < size; ++i) audio.l[i] = (position + i == 0) ? .1f : 0;
        audio.run(a.api);
        rendered.insert(rendered.end(), audio.outL.begin(), audio.outL.end()); position += size;
      }
      require(std::all_of(rendered.begin(), rendered.begin() + 64, [](float x) { return x == 0; }) && std::abs(rendered[64] - .1f) < .005f, "Variable-block quantum lost/delayed the impulse");
      a.api->reset(a.api);
      Audio silence(257); silence.run(a.api);
      require(std::all_of(silence.outL.begin(), silence.outL.end(), [](float x) { return x == 0; }), "Reset retained audio");
      Events trims; trims.add(0, -6, 13); trims.add(1, -3, 47); trims.add(3, 1, 80);
      Audio changed(256); changed.run(a.api, &trims);
      double value = 0;
      params->get_value(a.api, 0, &value); require(value == -6, "Automation did not update input trim");
      b.ext<clap_plugin_params_t>(CLAP_EXT_PARAMS)->get_value(b.api, 0, &value); require(value == 0, "Instances shared control state");
      Stream snapshot; require(state->save(a.api, &snapshot.out), "Cannot save state");
      auto* bState = b.ext<clap_plugin_state_t>(CLAP_EXT_STATE);
      require(bState->load(b.api, &snapshot.in) && otherHost.restart, "Active restore did not request a restart");
      b.stop(); b.start();
      b.ext<clap_plugin_params_t>(CLAP_EXT_PARAMS)->get_value(b.api, 0, &value); require(value == -6, "Project restore lost trim");
      Stream corrupt; corrupt.bytes = "{bad json}";
      require(!state->load(a.api, &corrupt.in), "Invalid state replaced preset");
      Stream excessive; excessive.bytes.assign(512 * 1024 + 1, 'x');
      require(!state->load(a.api, &excessive.in), "Unbounded state accepted");
      std::filesystem::create_directories(library / "models");
      std::filesystem::copy_file(ARDOR_NAM_EXAMPLE_MODEL, library / "models/test.nam");
      ardor::Preset nam; nam.name = "NAM with delay";
      nam.blocks.push_back({"amp", "nam", true, "models/test.nam", nlohmann::json::object()});
      nam.blocks.push_back({"echo", "delay", true, "", {{"mode", "digital"}, {"mix", .25}}});
      ardor::PresetStore store(library); store.save({0, 1}, nam);
      Events slot; slot.add(4, 1);
      Audio choose(32); choose.run(a.api, &slot);
      require(host.callback && !host.restart, "Preset selector loaded DSP in process");
      a.api->on_main_thread(a.api);
      require(host.restart && host.dirty, "Library selection failed to stage/restart");
      a.stop(); a.start();
      Audio guitar(2048);
      for (int i = 0; i < 2048; ++i) guitar.r[i] = .05f * std::sin(i * .05f);
      guitar.run(a.api);
      require(std::any_of(guitar.outL.begin(), guitar.outL.end(), [](float x) { return std::abs(x) > .00001f; }), "Full NAM chain produced no output (registrars/linkage)");
      Stream complete; require(state->save(a.api, &complete.out), "Full chain save failed");
      require(complete.bytes.find("models/test.nam") != std::string::npos && complete.bytes.find("digital") != std::string::npos, "Project state did not embed complete preset");
      std::filesystem::remove(store.pathFor({0, 1}));
      require(bState->load(b.api, &complete.in), "Project depended on original preset file");
      b.stop(); b.start(); guitar.run(b.api);
      // Host preset-load loads ordinary Ardor JSON on main, with relative assets.
      const auto* loader = a.ext<clap_plugin_preset_load_t>(CLAP_EXT_PRESET_LOAD);
      store.save({0, 2}, nam);
      require(loader && loader->from_location(a.api, CLAP_PRESET_DISCOVERY_LOCATION_FILE, clap_test::utf8(store.pathFor({0, 2})).c_str(), nullptr), "Native JSON preset-load failed");
      Stream beforeInvalid; require(state->save(a.api, &beforeInvalid.out), "Cannot snapshot prior preset");
      auto broken = nam; broken.blocks[0].asset = "models/missing.nam"; store.save({0, 3}, broken);
      require(!loader->from_location(a.api, CLAP_PRESET_DISCOVERY_LOCATION_FILE, clap_test::utf8(store.pathFor({0, 3})).c_str(), nullptr), "Missing model silently replaced chain");
      Stream afterInvalid; require(state->save(a.api, &afterInvalid.out) && afterInvalid.bytes == beforeInvalid.bytes,
                                   "Invalid preset changed pending/current state");
      // Parameter scenes and sequential WDW also run through the same module.
      ardor::Preset wdw; wdw.version = 3; wdw.routing = "wdw"; wdw.name = "WDW";
      wdw.wdw = ardor::WdwRouting{};
      wdw.wdw->dry.blocks.push_back({"dry-nam", "nam", true, "models/test.nam", nlohmann::json::object()});
      wdw.wdw->wet.blocks = nam.blocks;
      store.save({0, 2}, wdw);
      require(loader->from_location(a.api, CLAP_PRESET_DISCOVERY_LOCATION_FILE, clap_test::utf8(store.pathFor({0, 2})).c_str(), nullptr), "Sequential WDW load failed");
      a.stop(); a.start(); guitar.run(a.api);
      require(latency->get(a.api) >= 64, "WDW adapter latency missing");
      ardor::Preset scenes; scenes.version = 4; scenes.name = "Scene trem";
      scenes.blocks.push_back({"trem", "mod", true, "", {{"mode", "vintage_trem"}}});
      scenes.sceneSet = ardor::PresetSceneSet{};
      scenes.sceneSet->defaultSceneId = "s2";
      for (int i = 0; i < 4; ++i) {
        auto& scene = scenes.sceneSet->scenes[i];
        scene.id = "s" + std::to_string(i); scene.name = scene.id;
        scene.targets.push_back({ardor::PresetSceneTargetType::Parameter, "trem", "mix", "", i / 3.0f});
      }
      store.save({0, 2}, scenes);
      require(loader->from_location(a.api, CLAP_PRESET_DISCOVERY_LOCATION_FILE, clap_test::utf8(store.pathFor({0, 2})).c_str(), nullptr), "Parameter scene preset failed");
      a.stop(); a.start();
      params->get_value(a.api, 5, &value); require(value == 2, "Authored default scene lost on import");
      Events scene; scene.add(5, 3, 71); guitar.run(a.api, &scene);
      Stream sceneState; require(state->save(a.api, &sceneState.out), "Scene state save failed");
      require(bState->load(b.api, &sceneState.in), "Scene state restore failed");
      b.stop(); b.start(); guitar.run(b.api);
      b.ext<clap_plugin_params_t>(CLAP_EXT_PARAMS)->get_value(b.api, 5, &value); require(value == 3, "Scene selection not restored");
      // A host reset must not reuse a scene request ID. Recall mix=0 after a
      // previous scene request and compare audio against an independent clean
      // engine with the same host trims; control-value checks alone miss this.
      Host cleanHost; Plugin clean(factory, cleanHost);
      Events cleanControls; cleanControls.add(0, -6); cleanControls.add(1, -3); cleanControls.add(3, 1);
      clean.ext<clap_plugin_params_t>(CLAP_EXT_PARAMS)->flush(clean.api, &cleanControls.api, nullptr);
      clean.start(); b.api->reset(b.api);
      Events dryScene; dryScene.add(5, 0);
      Audio recalled(2048), reference(2048);
      // Allow the effect's normal control smoothing to settle, then compare.
      for (int pass = 0; pass < 6; ++pass) {
        for (int i = 0; i < 2048; ++i) recalled.r[i] = reference.r[i] = .05f * std::sin((pass * 2048 + i) * .05f);
        recalled.run(b.api, pass == 0 ? &dryScene : nullptr); reference.run(clean.api);
        if (pass >= 4)
          for (int i = 0; i < 2048; ++i)
            require(std::abs(recalled.outL[i] - reference.outL[i]) < 1e-5f, "Scene recall after host reset was ignored");
      }
      // In-place buffers and non-finite input remain bounded.
      guitar.out.data32 = guitar.inputs.data(); guitar.l[0] = std::numeric_limits<float>::quiet_NaN(); guitar.run(a.api);
      require(std::isfinite(guitar.l[0]), "In-place non-finite input escaped");
    }
    entry->deinit(); clap_test::close(handle); handle = nullptr;
    clap_test::environment(env, prior ? saved.c_str() : nullptr);
    std::filesystem::remove_all(root);
    std::cout << "CLAP ABI, lifecycle, variable blocks, state, library and NAM checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    if (handle) clap_test::close(handle);
    clap_test::environment(env, prior ? saved.c_str() : nullptr);
    std::filesystem::remove_all(root);
    std::cerr << e.what() << '\n'; return 1;
  }
}
