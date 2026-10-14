#include "dsp/NamProcessor.h"
#include "audio/EngineLoader.h"
#include "ui/UiModel.h"
#include "ui/ParameterControls.h"
#include "namparam/parametric_container.h"
#include "NAM/slimmable.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <limits>
#include <thread>
#include <atomic>
#include <array>
#include <cstdlib>
#include <new>

// Only live setter/audio calls enable this guard. Model loading is allowed to allocate.
thread_local bool auditRealtimeMemory = false;
thread_local size_t realtimeAllocations = 0, realtimeDeallocations = 0;
void* operator new(size_t size) {
  if (auditRealtimeMemory) ++realtimeAllocations;
  if (auto* ptr = std::malloc(std::max<size_t>(size, 1))) return ptr;
  throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* ptr) noexcept {
  if (auditRealtimeMemory && ptr) ++realtimeDeallocations;
  std::free(ptr);
}
void operator delete[](void* ptr) noexcept { ::operator delete(ptr); }
void operator delete(void* ptr, size_t) noexcept { ::operator delete(ptr); }
void operator delete[](void* ptr, size_t) noexcept { ::operator delete(ptr); }

namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const nlohmann::json& document) { std::ofstream(path) << document; }
float difference(const std::vector<float>& a, const std::vector<float>& b) {
  float sum = 0; for (size_t i=0; i<a.size(); ++i) sum += std::fabs(a[i]-b[i]); return sum/a.size();
}

void checkLiveRamp(const std::filesystem::path& path)
{
  // A one-layer, memoryless network makes the control trajectory observable
  // without a guitar waveform or recurrent-state transients hiding zipper noise.
  ardor::NamProcessor moving, chunked, baseline, target;
  for (auto* processor : {&moving, &baseline})
    require(processor->load(path, 48000, 64, 1, std::nullopt, {{"Drive", 0}}), "ramp start loads");
  require(chunked.load(path, 48000, 13, 1, std::nullopt, {{"Drive", 0}}), "small ramp blocks load");
  require(target.load(path, 48000, 64, 1, std::nullopt, {{"Drive", 10}}), "ramp destination loads");
  const float start = baseline.process(0.05f), end = target.process(0.05f);
  require(std::fabs(end - start) > 0.01f, "test network exposes learned control");
  require(!moving.setParameterTarget("Missing", 1)
    && !moving.setParameterTarget("Drive", 11)
    && !moving.setParameterTarget("Drive", std::numeric_limits<float>::quiet_NaN()), "invalid live values rejected");
  require(moving.setParameterTarget("Drive", 10) && chunked.setParameterTarget("Drive", 10), "live targets accepted");
  std::vector<float> input(12000, 0.05f), output(input.size()), other(input.size());
  moving.processBlock(input.data(), output.data(), 1000);
  chunked.processBlock(input.data(), other.data(), 1000);
  require(std::fabs(output.front() - start) < 0.000001f, "first changed sample retains original value");
  require(moving.setParameterTarget("Drive", 3) && chunked.setParameterTarget("Drive", 3), "mid-ramp retarget accepted");
  moving.processBlock(input.data() + 1000, output.data() + 1000, input.size() - 1000);
  chunked.processBlock(input.data() + 1000, other.data() + 1000, input.size() - 1000);
  float peakStep = 0;
  for (size_t i = 1; i < output.size(); ++i)
    peakStep = std::max(peakStep, std::fabs(output[i] - output[i - 1]));
  require(peakStep < std::fabs(end - start) / 1000, "ramp and retarget avoid abrupt output steps");
  require(difference(output, other) < 0.00001f, "live ramp timing is independent of host block size");
  require(target.setParameterTarget("Drive", 3), "comparison target accepted");
  target.reset(); // reset must retain the newest committed live target
  require(std::fabs(output.back() - target.process(0.05f)) < 0.000001f, "ramp settles exactly at latest target");
  std::cout << "Live concat ramp peak step " << peakStep << " versus full jump " << std::fabs(end - start) << "\n";
}

void checkConcurrentTargets(const std::filesystem::path& path, const std::string& name,
                            float maximum, float tier)
{
  ardor::NamProcessor processor;
  require(processor.load(path, 48000, 64, tier), "concurrent model loads");
  std::atomic<bool> finished{false};
  bool finite = true;
  size_t audioAllocations = 0, audioDeallocations = 0;
  std::thread audio([&] {
    std::array<float, 64> input{}, output{};
    auditRealtimeMemory = true;
    for (size_t block = 0; block < 256 || !finished.load(std::memory_order_acquire); ++block) {
      for (size_t i = 0; i < input.size(); ++i) input[i] = 0.1f * std::sin((block * 64 + i) * 0.17f);
      processor.processBlock(input.data(), output.data(), input.size());
      for (float sample : output) finite &= std::isfinite(sample);
    }
    auditRealtimeMemory = false;
    audioAllocations = realtimeAllocations;
    audioDeallocations = realtimeDeallocations;
  });
  bool accepted = true;
  const auto start = std::chrono::steady_clock::now();
  realtimeAllocations = realtimeDeallocations = 0;
  auditRealtimeMemory = true;
  for (size_t i = 0; i < 100000; ++i)
    accepted &= processor.setParameterTarget(name, maximum * static_cast<float>(i % 101) / 100.0f);
  auditRealtimeMemory = false;
  const auto elapsed = std::chrono::steady_clock::now() - start;
  finished.store(true, std::memory_order_release);
  audio.join();
  require(accepted && finite, "concurrent parameter sweeps remain valid and finite");
  require(audioAllocations == 0 && audioDeallocations == 0
    && realtimeAllocations == 0 && realtimeDeallocations == 0, "live setters and audio processing must not allocate or free C++ storage");
  std::cout << "Live target " << name << " tier " << tier << " mean "
    << std::chrono::duration<double, std::micro>(elapsed).count() / 100000 << " us\n";
}

void checkLiveFilmHistory(const std::filesystem::path& path, float tier)
{
  nlohmann::json document;
  std::ifstream(path) >> document;
  auto reference = nam::ardor_parametric::create(document);
  dynamic_cast<nam::SlimmableModel&>(*reference).SetSlimmableSize(tier);
  reference->ResetAndPrewarm(48000, 64);
  auto& controls = dynamic_cast<nam::IParametricControl&>(*reference);
  ardor::NamProcessor processor;
  require(processor.load(path, 48000, 64, tier), "live FiLM reference loads");
  std::array<float, 64> input{}, actual{}, expected{};
  std::array<float, 2> values{0.5f, 0.5f};
  float error = 0;
  for (size_t block = 0; block < 384; ++block) {
    if (block == 9 || block == 37 || block == 45 || block == 80) {
      values[0] = block == 9 ? 1.0f : 0.2f;
      values[1] = block == 37 ? 0.9f : 0.1f;
      require(processor.setParameterTarget("Gain", values[0])
        && processor.setParameterTarget("Tone", values[1]), "FiLM live retargets accepted");
      controls.SetParams(values);
    }
    for (size_t i = 0; i < input.size(); ++i) input[i] = 0.1f * std::sin((block * 64 + i) * 0.17f);
    float* in[] = {input.data()};
    float* out[] = {expected.data()};
    reference->process(in, out, 64);
    processor.processBlock(input.data(), actual.data(), 64);
    for (size_t i = 0; i < actual.size(); ++i) {
      require(std::isfinite(actual[i]), "live FiLM output remains finite");
      error = std::max(error, std::fabs(actual[i] - expected[i]));
    }
  }
  require(error < 0.000001f, "live FiLM matches uninterrupted smoothed DSP without resets");
  std::cout << "Live FiLM tier " << tier << " uninterrupted reference error " << error << "\n";
}
}
int main() {
  const auto source = std::filesystem::path{ARDOR_SOURCE_DIR};
  const auto example = ARDOR_NAM_EXAMPLE_MODEL;
  const auto model = source/"assets/nam/Ardor-Parametric-Overdrive-Demo.nam";
  ardor::NamProcessor low, high, bright, chunked;
  require(low.load(model,48000,64,1,std::nullopt,{{"Drive",0},{"Tone",5}}),"demo loads with controls");
  require(high.load(model,48000,64,1,std::nullopt,{{"Drive",10},{"Tone",5}}),"high drive loads");
  require(bright.load(model,48000,64,1,std::nullopt,{{"Drive",10},{"Tone",10}}),"bright tone loads");
  require(chunked.load(model,48000,13,1,std::nullopt,{{"Drive",10},{"Tone",5}}),"chunked model loads");
  std::vector<float> input(4096), a(input.size()),b(input.size()),c(input.size()),d(input.size());
  for (size_t i=0;i<input.size();++i) input[i]=0.1f*std::sin(i*0.27f)+0.02f*std::sin(i*1.2f);
  low.processBlock(input.data(),a.data(),input.size()); high.processBlock(input.data(),b.data(),input.size());
  bright.processBlock(input.data(),c.data(),input.size()); chunked.processBlock(input.data(),d.data(),input.size());
  require(difference(a,b)>0.015f,"drive must change neural audio");
  require(difference(b,c)>0.004f,"tone must change neural audio");
  require(difference(b,d)<0.00001f,"model is independent of host block size");
  for (float v:b) require(std::isfinite(v) && std::fabs(v)<1,"demo stays finite and bounded");
  high.reset(); chunked.reset();
  high.processBlock(input.data(),b.data(),input.size()); chunked.processBlock(input.data(),d.data(),input.size());
  require(difference(b,d)<0.00001f,"reset preserves committed controls");
  require(!low.load(model,48000,64,1,std::nullopt,{{"Drive",11}}),"reject out of range controls");
  require(!low.load(model,48000,64,1,std::nullopt,{{"Missing",1}}),"reject unknown controls");
  require(!low.load(model,44100,64),"reject unsupported sample rate");
  const auto root=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std::filesystem::create_directories(root/"models");
  std::filesystem::copy_file(model,root/"models/overdrive.nam");
  std::filesystem::copy_file(example,root/"models/amp.nam");
  // A switch is encoded as two independent one-hot inputs. Exercise a small
  // recurrent network with known weights, not a UI-only parameter fixture.
  nlohmann::json spec={{"version","1.0.0"},{"architecture","ConcatLSTM"},{"sample_rate",48000},
    {"config",{{"hidden_size",1},{"num_layers",1},{"params",nlohmann::json::array({
      {{"name","Drive"},{"min",0},{"max",10},{"default",5},{"type","continuous"}},
      {{"name","Mode"},{"min",0},{"max",1},{"default",0},{"type","switch"},{"enum_names",{"Soft","Hard"}}}
    })}}}};
  std::vector<float> weights(28,0); weights[10]=3; weights[18]=4; // candidate audio; mode into output gate
  weights[20]=4; weights[21]=-8; weights[23]=-2; weights[26]=0.3f;
  spec["weights"]=weights; write(root/"models/switch.nam",spec);
  require(low.load(root/"models/switch.nam",48000,64,1,std::nullopt,{{"Mode",0}}),"switch model loads");
  require(high.load(root/"models/switch.nam",48000,64,1,std::nullopt,{{"Mode",1}}),"alternate switch loads");
  low.processBlock(input.data(),a.data(),input.size());high.processBlock(input.data(),b.data(),input.size());
  require(difference(a,b)>0.01f,"one-hot switch must affect audio");
  require(!low.setParameterTarget("Mode", 0.5f), "live switches reject fractional indices");
  require(low.setParameterTarget("Mode", 1), "live switch accepts exact option");
  low.processBlock(input.data(), a.data(), input.size());
  require(difference(a,b)<0.00001f, "live switch retains exact one-hot conditioning");
  require(!low.load(root/"models/switch.nam",48000,64,1,std::nullopt,{{"Mode",0.5}}),"reject fractional switches");
  auto wrongRate = spec;
  wrongRate["sample_rate"] = 44100; write(root/"models/rate.nam",wrongRate);
  require(!low.load(root/"models/rate.nam",48000,64),"reject incompatible model rate");
  wrongRate.erase("sample_rate"); write(root/"models/rate.nam",wrongRate);
  require(!low.load(root/"models/rate.nam",48000,64),"require exported model rate");
  spec["weights"]=nlohmann::json::array({0}); write(root/"models/short.nam",spec);
  require(!low.load(root/"models/short.nam",48000,64),"reject truncated weights");
  spec["version"]="0.7.0";write(root/"models/version.nam",spec);
  require(!low.load(root/"models/version.nam",48000,64),"reject snapshot version on parametric model");
  nlohmann::json wave={{"version","1.0.0"},{"architecture","ConcatWaveNet"},{"sample_rate",48000},
    {"config",{{"params",nlohmann::json::array({{{"name","Drive"},{"min",0},{"max",10},{"default",5}}})},
      {"layers",nlohmann::json::array({{{"input_size",2},{"condition_size",2},{"head_size",1},
        {"channels",1},{"kernel_size",1},{"dilations",{1}},{"activation","Tanh"},{"gated",false},{"head_bias",true}}})},
      {"head",nullptr},{"head_scale",0.1}}},
    {"weights",{1.0,0.0, 1.0,0.0, 0.0,1.0, 1.0,0.0, 1.0,0.0, 0.1}}};
  write(root/"models/wave.nam",wave);
  require(low.load(root/"models/wave.nam",48000,64,1,std::nullopt,{{"Drive",0}}),"ConcatWaveNet loads");
  require(high.load(root/"models/wave.nam",48000,64,1,std::nullopt,{{"Drive",10}}),"ConcatWaveNet controls load");
  low.processBlock(input.data(),a.data(),input.size()); high.processBlock(input.data(),b.data(),input.size());
  require(difference(a,b)>0.01,"WaveNet condition channels affect audio");
  checkLiveRamp(root/"models/wave.nam");
  wave["weights"]={0.0};write(root/"models/wave-short.nam",wave);
  require(!low.load(root/"models/wave-short.nam",48000,64),"reject truncated WaveNet weights before reading them");
  const auto clone = source/"assets/nam/clone-overdrive.nam";
  checkConcurrentTargets(model, "Drive", 10, 1);
  checkConcurrentTargets(clone, "Gain", 1, 0);
  checkConcurrentTargets(clone, "Gain", 1, 1);
  checkLiveFilmHistory(clone, 0);
  checkLiveFilmHistory(clone, 1);
  std::filesystem::copy_file(clone,root/"models/clone.nam");
  nlohmann::json cloneDocument; std::ifstream(clone) >> cloneDocument;
  for (float tier : {0.0f, 1.0f}) {
    require(low.load(clone,48000,64,tier,std::nullopt,{{"Gain",0},{"Tone",0}}),"Clone low controls load");
    require(high.load(clone,48000,64,tier,std::nullopt,{{"Gain",1},{"Tone",0}}),"Clone gain loads");
    require(bright.load(clone,48000,64,tier,std::nullopt,{{"Gain",1},{"Tone",1}}),"Clone tone loads");
    low.processBlock(input.data(),a.data(),input.size()); high.processBlock(input.data(),b.data(),input.size());
    bright.processBlock(input.data(),c.data(),input.size());
    std::cout << "Clone tier " << tier << " gain difference " << difference(a,b) << " tone difference " << difference(b,c) << "\n";
    require(difference(a,b)>0.001f,"Clone Gain changes neural audio in both tiers");
    require(difference(b,c)>0.001f,"Clone Tone changes neural audio in both tiers");
    for (float value : c) require(std::isfinite(value),"Clone audio remains finite");
    require(high.slimmableSizeBreakpoints()==std::vector<double>{0.5},"Clone exposes original tier breakpoint");
    require(chunked.load(clone,48000,13,tier,std::nullopt,{{"Gain",1},{"Tone",0}}),"Clone custom block size loads");
    chunked.processBlock(input.data(),d.data(),input.size());
    require(difference(b,d)<0.00001f,"FiLM restored controls are independent of buffer size");
    high.reset();chunked.reset();
    high.processBlock(input.data(),b.data(),input.size());chunked.processBlock(input.data(),d.data(),input.size());
    require(difference(b,d)<0.00001f,"FiLM reset preserves restored controls");
  }
  // Golden samples come from the fork's generic Eigen WaveNet, independently
  // constructed from the unmodified release weights (provenance beside fixture).
  nlohmann::json references;
  std::ifstream(source/"tests/fixtures/clone-overdrive-reference.json") >> references;
  for (size_t tier = 0; tier < references.size(); ++tier) {
    require(low.load(clone,48000,64,tier==0 ? 0.0f : 1.0f),"Clone default controls load");
    low.processBlock(input.data(),a.data(),input.size());
    for (const auto& point : references[tier]["samples"]) {
      const size_t frame=point["frame"].get<size_t>();
      require(std::fabs(a[frame]-point["value"].get<float>())<0.00002f,"FiLM fast kernels match independent upstream generic reference");
    }
  }
  auto badClone = cloneDocument;
  const auto invalidClone = root/"models/invalid-film.nam";
  auto rejectsFilm = [&](const nlohmann::json& bad, const char* message) {
    write(invalidClone,bad); require(!low.load(invalidClone,48000,64),message);
  };
  badClone["config"]["submodels"][0]["model"]["weights"].erase(0);
  rejectsFilm(badClone,"reject truncated FiLM weights");
  badClone=cloneDocument;badClone["config"]["submodels"][1]["model"]["weights"].push_back(0);
  rejectsFilm(badClone,"reject trailing FiLM weights");
  for (const char* key : {"schema_version","head_mode","type","condition_size"}) {
    badClone=cloneDocument; auto& par=badClone["config"]["submodels"][0]["model"]["config"]["parametric"];
    if (std::string(key)=="schema_version") par[key]=2;
    else if (std::string(key)=="condition_size") par[key]=3;
    else par[key]="unsupported";
    rejectsFilm(badClone,"reject unsupported FiLM schema/head/type/condition size");
  }
  badClone=cloneDocument;badClone["config"]["submodels"][1]["model"]["config"]["parametric"]["parameters"][0]["default"]=0.2;
  rejectsFilm(badClone,"reject incompatible tier controls");
  badClone=cloneDocument;badClone["config"]["submodels"][0]["max_value"]=1;
  rejectsFilm(badClone,"reject overlapping tier boundaries");
  nlohmann::json dualExport=wave;dualExport["embedded_parametric"]=cloneDocument;
  write(root/"models/dual-export.nam",dualExport);
  require(low.load(root/"models/dual-export.nam",48000,64,1,std::nullopt,{{"Gain",0.7},{"Tone",0.2}}),"prefer embedded parametric model over baked snapshot");
  ardor::Preset preset; preset.name="Parametric rig";
  preset.blocks={{"od","namPedal",true,"models/overdrive.nam",{{"modelParameters",{{"Drive",7},{"Tone",3}}}}},
    {"amp","nam",true,"models/amp.nam",nlohmann::json::object()}};
  ardor::PedalEngine engine; ardor::EngineLoadOptions options;std::string error;
  require(ardor::applyPreset(engine,preset,root,options,error),"pedal before snapshot amp applies: "+error);
  ardor::RuntimeChain chain;
  require(ardor::prepareRuntimeChain(chain,ardor::buildChainPlan(preset,root).blocks,options,error),"pedal+amp lane applies: "+error);
  require(engine.setNamParameter("od", "Drive", 8) && chain.setNamParameter("od", "Tone", 4), "serial controls reach the live pedal");
  require(!engine.setNamParameter("amp", "Drive", 8) && !chain.setNamParameter("missing", "Drive", 8), "snapshot and missing blocks reject learned controls");
  ardor::Preset dual;
  dual.version = 2;
  dual.blocks = {{"rig", "dualRig", true, "", nlohmann::json::object()}};
  dual.blocks[0].lanes[0] = {{"left-od", "namPedal", true, "models/overdrive.nam", nlohmann::json::object()}};
  dual.blocks[0].lanes[1] = {{"right-od", "namPedal", true, "models/overdrive.nam", nlohmann::json::object()}};
  require(ardor::applyPreset(engine, dual, root, options, error), "live Dual Rig prepares: " + error);
  require(engine.setNamParameter("left-od", "Drive", 8)
    && engine.setNamParameter("right-od", "Tone", 4), "live controls reach both Dual Rig children");
  auto ui=ardor::makeDemoUiState();ardor::loadAssetsFromDataRoot(ui,root);ardor::replaceActivePreset(ui,preset);
  const auto asset=std::find_if(ui.assets.begin(),ui.assets.end(),[](const auto& a){return a.path=="models/overdrive.nam";});
  require(asset!=ui.assets.end() && asset->blockType=="namPedal","native drawer classifies parametric pedal");
  ui.selectedBlock=0;ui.paramTarget=ardor::UiParamTarget::Block;
  const auto controls=ardor::parameterPage(ui,0);
  require(std::any_of(controls.begin(),controls.end(),[](const auto& c){return c.key=="model:Drive" && c.value==7;}),"native controls restore saved values");
  const auto roundtrip=ardor::activePresetToPreset(ui);
  require(roundtrip.blocks[0].params["modelParameters"]["Tone"]==3,"native roundtrip preserves parameters");
  ardor::setUiStatus(ui, "Saved preset");
  const auto statusRevision = ui.revisions.status;
  ardor::setSelectedBlockParam(ui, "model:Drive", 8);
  require(!ardor::pendingStructuralPreview(ui), "learned control must not rebuild the chain");
  require(ui.revisions.status == statusRevision && ui.statusMessage == "Saved preset",
          "learned control success does not trigger a toast");
  require(ardor::activePresetToPreset(ui).blocks[0].params["modelParameters"]["Drive"] == 8,
          "live control edit retains the new value");
  ardor::setSelectedBlockParam(ui, "model:Drive", 9);
  require(!ardor::pendingStructuralPreview(ui), "repeated learned edits remain immediately available");
  auto fallback = ardor::captureUiPreviewSnapshot(ui);
  ardor::setSelectedBlockParam(ui, "model:Drive", 10);
  require(ardor::queuePreview(ui, fallback, "failed live control", false), "failed live updates can fall back to prepared preview");
  ardor::failStructuralPreview(ui, "test failure");
  require(ui.statusIsError && ui.statusMessage == "Could not apply chain: test failure",
          "failed control preview still explains the error");
  require(ardor::activePresetToPreset(ui).blocks[0].params["modelParameters"]["Drive"] == 9,
          "failed control preview restores the audible value");
  auto clonePreset=preset;
  clonePreset.blocks[0].asset="models/clone.nam";
  clonePreset.blocks[0].params={{"useNano",true},{"modelParameters",{{"Gain",0.8},{"Tone",0.1}}}};
  require(ardor::applyPreset(engine,clonePreset,root,options,error),"original Clone pedal+amp applies: "+error);
  ardor::replaceActivePreset(ui,clonePreset); ui.selectedBlock=0;
  const auto cloneControls=ardor::parameterPage(ui,0);
  require(std::any_of(cloneControls.begin(),cloneControls.end(),[](const auto& c){return c.key=="model:Gain" && std::fabs(c.value-0.8)<0.0001;}),"native FiLM controls restore saved values");
  require(std::any_of(cloneControls.begin(),cloneControls.end(),[](const auto& c){return c.key=="useNano" && c.value==1;}),"native FiLM exposes compact tier");
  const auto cloneRoundtrip=ardor::activePresetToPreset(ui);
  require(cloneRoundtrip.blocks[0].params["modelParameters"]["Tone"]==0.1 && cloneRoundtrip.blocks[0].params["useNano"]==true,"FiLM preset roundtrip retains values and tier");
  const auto sizeControl=std::find_if(cloneControls.begin(),cloneControls.end(),[](const auto& c){return c.key=="useNano";});
  const auto beforeSizeStatus=ui.revisions.status;
  require(ardor::applyParameterDelta(ui,*sizeControl,-1),"native pedal size selector changes Nano to Standard");
  require(ardor::pendingStructuralPreview(ui),"native pedal tier change queues an audio preview");
  ardor::completeStructuralPreview(ui);
  const auto standardClone=ardor::activePresetToPreset(ui);
  require(standardClone.blocks[0].params["useNano"]==false
    && standardClone.blocks[0].params["modelParameters"]==cloneRoundtrip.blocks[0].params["modelParameters"],
    "native tier change preserves learned controls");
  require(ui.revisions.status==beforeSizeStatus,"native tier change is silent on success");
  require(ardor::applyPreset(engine,standardClone,root,options,error),"native Standard pedal selection applies: "+error);
  ardor::Preset wdw; wdw.version=3; wdw.routing="wdw"; wdw.name="Parametric WDW";
  wdw.wdw=ardor::WdwRouting{};
  wdw.wdw->dry.blocks={{"dry-amp","nam",true,"models/amp.nam",nlohmann::json::object()}};
  wdw.wdw->wet.blocks={{"wet-amp","nam",true,"models/amp.nam",nlohmann::json::object()}};
  const auto pedalAssetIndex=static_cast<size_t>(asset-ui.assets.begin());
  for (size_t lane=0;lane<2;++lane) {
    auto wdwUi=ui; ardor::replaceActivePreset(wdwUi,wdw); ardor::enterEditMode(wdwUi);
    ardor::insertLaneAssetBlock(wdwUi,pedalAssetIndex,0,lane,0);
    require(wdwUi.bank.presets[wdwUi.activePreset].blocks[0].lanes[lane].size()==2,
      "native WDW lane must admit a neural pedal before its amp");
    auto stored=ardor::activePresetToPreset(wdwUi);
    require(ardor::applyPreset(engine,stored,root,options,error),"WDW neural pedal applies: "+error);
    const auto pedalId = stored.wdw ? (lane == 0 ? stored.wdw->dry.blocks[0].id : stored.wdw->wet.blocks[0].id)
      : stored.blocks[0].lanes[lane][0].id;
    require(engine.setNamParameter(pedalId, "Drive", 8), "live controls reach each WDW lane");
  }
  std::filesystem::remove_all(root);
  std::cout<<"parametric NAM audio, controls, routing and native roundtrip passed\n";
}
