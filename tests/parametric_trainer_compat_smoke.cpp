#include "dsp/NamProcessor.h"
#include "FlatHyperWaveNetFixture.h"
#include "preset/NamMetadata.h"
#include "ui/UiModel.h"
#include "ui/ParameterControls.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>

namespace {
void require(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }

void checkFlatHyperContainer(const std::filesystem::path& source, const std::filesystem::path& models,
                             const std::vector<float>& input) {
  using Json = nlohmann::json;
  const FlatHyperWaveNetFixture fixture(source);
  const auto& document = fixture.document;
  const auto path = models / "flat-hyper.nam";
  std::ofstream(path) << document;
  ardor::NamProcessor model, reference;
  std::vector<float> output(input.size()), expected(input.size());
  constexpr std::size_t rampSamples = 240;
  require(input.size() > rampSamples, "fixture must cover the whole volume ramp");
  for (const int blockSize : {13, 64}) {
    for (const float size : {0.0f, 0.5f}) {
      for (const auto [volume, gain] : {std::pair{2.0f, 0.016f}, std::pair{5.0f, 0.25f}, std::pair{7.0f, 0.558f}}) {
        Json parameters{{"gain", 7}};
        if (volume != 5.0f) parameters["Volume"] = volume;
        require(model.load(path, 48000, blockSize, size, std::nullopt, parameters),
                "flat HyperWaveNet loads even with parent-only capture metadata");
        require(model.slimmableSizeBreakpoints() == std::vector<double>{0.5}, "flat HyperWaveNet exposes both tiers");
        require(reference.loadDocument(fixture.tiers[size == 0.0f ? 0 : 1], 48000, blockSize, 1, std::nullopt, {{"gain", 7}}),
                "direct reference tier loads");
        model.processBlock(input.data(), output.data(), input.size());
        reference.processBlock(input.data(), expected.data(), input.size());
        for (std::size_t i = 0; i < output.size(); ++i)
          require(std::fabs(output[i] - gain * expected[i]) < 0.00005f, "volume follows both cubic segments");

        require(model.setParameterTarget("Volume", 0), "flat HyperWaveNet exposes live volume");
        model.processBlock(input.data(), output.data(), input.size());
        reference.processBlock(input.data(), expected.data(), input.size());
        for (std::size_t i = 0; i < rampSamples; ++i) {
          const float rampGain = gain * (1.0f - static_cast<float>(i) / rampSamples);
          require(std::fabs(output[i] - rampGain * expected[i]) < 0.00005f, "volume ramps linearly for 5 ms");
        }
        require(output[rampSamples - 1] != 0.0f, "volume still has gain before the ramp ends");
        require(std::all_of(output.begin() + rampSamples, output.end(), [](float value) { return value == 0.0f; }),
                "volume reaches exact mute at 240 samples");
      }
    }
  }
  model.setSlimmableSize(0);
  model.processBlock(input.data(),output.data(),input.size());
  require(std::all_of(output.begin(),output.end(),[](float v) { return v == 0; }), "tier changes preserve volume");
  auto bad = document;
  bad["config"]["submodels"][1]["weight_offset"] = document.at("weights").size();
  require(!model.loadDocument(bad,48000,64), "invalid flat tier weights are rejected");
  bad = document;
  bad["config"]["params"][0]["default"] = 6;
  require(!model.loadDocument(bad,48000,64), "mismatched playback controls are rejected");
  bad = document;
  bad["extensions"]["linear_volume"]["segments"][1]["right"] = 9;
  require(!model.loadDocument(bad,48000,64), "incomplete volume curve is rejected");
}
}
int main() {
  const auto source = std::filesystem::path{ARDOR_SOURCE_DIR};
  nlohmann::json fixture; std::ifstream(source/"tests/fixtures/parametric-trainer-a2.json") >> fixture;
  nlohmann::json dualFixture;
  std::ifstream(source/"tests/fixtures/parametric-trainer-a2-dual.json") >> dualFixture;
  require(fixture.at("input") == dualFixture.at("input"), "fixtures share the reference input");
  for (const auto& model : dualFixture.at("models")) fixture["models"].push_back(model);
  nlohmann::json filmFixture;
  std::ifstream(source/"tests/fixtures/parametric-trainer-film.json") >> filmFixture;
  require(fixture.at("input") == filmFixture.at("input"), "FiLM fixtures share the reference input");
  for (const auto& model : filmFixture.at("models")) fixture["models"].push_back(model);
  const auto input = fixture.at("input").get<std::vector<float>>();
  std::vector<float> output(input.size());
  const auto root = std::filesystem::temp_directory_path()/ ("ardor-trainer-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root/"models");
  size_t index = 0;
  for (const auto& entry : fixture.at("models")) {
    const auto& document = entry.at("model");
    const auto label = entry.at("label").get<std::string>();
    const auto specs = ardor::namParameterSpecs(document);
    require(!specs.empty(), "trainer definitions load: " + label);
    if (specs.size() == 24) {
      auto overflow = document;
      auto extra = specs.back(); extra["name"] = "Too many controls";
      overflow["config"]["params"].push_back(extra);
      bool rejected = false;
      try { ardor::namParameterSpecs(overflow); }
      catch (const std::runtime_error& error) { rejected = std::string(error.what()).find("1 to 24") != std::string::npos; }
      require(rejected, "device rejects controls beyond the trainer limit");
    }
    const auto file = root/"models"/(std::to_string(index++) + ".nam");
    std::ofstream(file) << document;
    for (const auto& audio : entry.at("cases")) {
      for (int blockSize : {13, 64}) {
        ardor::NamProcessor processor;
        require(processor.load(file,48000,blockSize,audio.value("slimmable_size",1.0f),std::nullopt,audio.at("parameters")), "trainer export loads: " + label);
        processor.processBlock(input.data(),output.data(),input.size());
        const auto expected = audio.at("output").get<std::vector<float>>();
        require(expected.size()==output.size(), "reference frame count matches");
        float error = 0;
        for (size_t i = 0; i < output.size(); ++i) {
          require(std::isfinite(output[i]), "trainer audio remains finite");
          error = std::max(error,std::fabs(output[i]-expected[i]));
        }
        std::cout << label << " block " << blockSize << " max Python error " << error << "\n";
        require(error < 0.00005f, "Ardor audio matches trainer inference: " + label);
      }
    }
    if (document.at("architecture") == "SlimmableContainer") {
      ardor::NamProcessor processor;
      const auto& defaults = entry.at("cases").at(0).at("parameters");
      require(processor.load(file,48000,64,0,std::nullopt,defaults), "combined export loads Nano with controls");
      require(processor.slimmableSizeBreakpoints() == std::vector<double>{0.5}, "combined export offers two tiers");
      for (float size : {1.0f,0.0f}) {
        processor.setSlimmableSize(size);
        processor.processBlock(input.data(),output.data(),input.size());
        const auto expected = std::find_if(entry.at("cases").begin(),entry.at("cases").end(),[&](const auto& audio) {
          return audio.at("parameters") == defaults && audio.at("slimmable_size") == size;
        });
        require(expected != entry.at("cases").end(), "tier reference exists");
        const auto reference = expected->at("output").template get<std::vector<float>>();
        for (size_t i=0;i<output.size();++i)
          require(std::fabs(output[i]-reference[i]) < 0.00005f, "switching tiers preserves controls and tier normalization");
      }
      for (const std::string fault : {"controls","version","calibration","boundary","family","weights"}) {
        auto bad = document;
        auto& child = bad["config"]["submodels"][0]["model"];
        const bool film = child["architecture"] == "ParametricWaveNet";
        if (fault == "controls") {
          auto& parameters = film ? child["config"]["parametric"]["parameters"] : child["config"]["params"];
          parameters[0]["default"] = 3;
        }
        else if (fault == "version") child["version"] = "invalid";
        else if (fault == "calibration") {
          if (film) child["config"]["parametric"]["schema_version"] = 2;
          else child["metadata"]["input_level_dbu"] = 6.4;
        }
        else if (fault == "boundary") bad["config"]["submodels"][0]["max_value"] = 1;
        else if (fault == "family") child["architecture"] = film ? "ConcatWaveNet" : "ParametricWaveNet";
        else child["weights"].erase(0);
        const auto malformed = root/("bad-"+fault+".nam");
        std::ofstream(malformed) << bad;
        require(!processor.load(malformed,48000,64), "reject malformed combined export: " + fault);
      }
    }
  }
  checkFlatHyperContainer(source, root/"models", input);
  auto ui = ardor::makeDemoUiState(); ardor::loadAssetsFromDataRoot(ui,root);
  size_t loaded = 0;
  for (const auto& asset : ui.assets) {
    if (asset.modelParameterSpecs.empty()) continue;
    nlohmann::json document; std::ifstream(root/asset.path) >> document;
    const auto expected = document.at("metadata").at("gear_type") == "pedal" ? "namPedal" : "nam";
    require(asset.blockType == expected, "device preserves trainer amp/pedal classification");
    require(asset.modelParameterSpecs == ardor::namParameterSpecs(document), "device preserves every exported control");
    if (document.at("architecture") == "ParametricSlimmableContainer") {
      require(asset.modelSlimmable && asset.modelParameterSpecs.at(1).at("name") == "Volume",
              "device drawer exposes flat tiers and the independent volume control");
    } else if (document.at("architecture") == "SlimmableContainer") {
      require(asset.modelSlimmable, "device advertises both bundled tiers");
      ardor::Preset preset; preset.name = "Combined trainer rig";
      const auto lastName = asset.modelParameterSpecs.back().at("name").get<std::string>();
      const auto lastValue = asset.modelParameterSpecs.back().at("max");
      const nlohmann::json values = {{"Drive",7},{lastName,lastValue}};
      preset.blocks = {{"model",expected,true,asset.path,{{"useNano",true},{"modelParameters",values}}}};
      ardor::replaceActivePreset(ui,preset); ui.selectedBlock=0; ui.paramTarget=ardor::UiParamTarget::Block;
      const auto controls = ardor::parameterPage(ui,0);
      require(std::any_of(controls.begin(),controls.end(),[](const auto& control) {
        return control.key == "useNano" && control.value == 1 && control.label == "Model size";
      }), "device restores Nano selector");
      const auto saved = ardor::activePresetToPreset(ui);
      require(saved.blocks[0].params["useNano"] == true && saved.blocks[0].params["modelParameters"][lastName] == lastValue,
              "device preserves bundled tier and learned controls in presets");
    }
    ++loaded;
  }
  require(loaded == fixture.at("models").size() + 1, "device drawer discovers every trainer export");
  nlohmann::json clone;
  std::ifstream(std::filesystem::path{ARDOR_SOURCE_DIR}/"assets/nam/clone-overdrive.nam") >> clone;
  auto film = clone.at("config").at("submodels").at(0).at("model");
  auto& conditioning = film["config"]["parametric"];
  conditioning["parameters"] = nlohmann::json::array();
  for (int i = 0; i < 17; ++i)
    conditioning["parameters"].push_back({{"name", "Control " + std::to_string(i)}, {"min",0}, {"max",1}, {"default",0.5}});
  conditioning["condition_size"] = 17;
  bool rejected = false;
  try { ardor::namParameterSpecs(film); }
  catch (const std::runtime_error& error) { rejected = std::string(error.what()).find("1 to 16") != std::string::npos; }
  require(rejected, "FiLM retains its separate 16-control limit");
  std::filesystem::remove_all(root);
}
