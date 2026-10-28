// SPDX-License-Identifier: MIT
#include "TestHost.h"

int main(){try{
  auto* library=clap_test::open(NAM_CLAP_BINARY);require(library,clap_test::error());
  auto* entry=static_cast<const clap_plugin_entry_t*>(clap_test::symbol(library,"clap_entry"));require(entry&&entry->init(NAM_CLAP_BINARY),"entry");
  auto* factory=static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  require(factory&&factory->get_plugin_count(factory)==1&&!factory->get_plugin_descriptor(factory,1),"factory enumeration");
  require(!factory->create_plugin(factory,nullptr,"bad"),"invalid creation rejected");
  {
    Instance instance(factory);require(instance.params->count(instance.plugin)==6,"empty plugin has only I/O controls");
    auto empty=instance.save();
    auto copied=std::filesystem::temp_directory_path()/std::filesystem::path(u8"Ardor-NAM-Bássman-");
    copied+=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".nam";
    std::filesystem::copy_file(NAM_CLAP_DEMO,copied);const bool loaded=instance.loadFile(copied);std::filesystem::remove(copied);
    require(loaded,"load real parametric model from a Unicode path");require(instance.params->count(instance.plugin)==8,"model controls exposed");
    const auto drive=instance.find("Drive"),tone=instance.find("Tone");require(drive!=CLAP_INVALID_ID&&tone!=CLAP_INVALID_ID&&drive!=tone,"distinct named controls");
    require(instance.find("Model size")==CLAP_INVALID_ID,"single model has no fictitious size control");
    char text[128];double value;
    require(instance.params->value_to_text(instance.plugin,Input,-3,text,sizeof(text))&&!std::strcmp(text,"-3.00 dB"),"gain units");
    require(instance.params->text_to_value(instance.plugin,Input,"-3 dB",&value)&&value==-3,"parse gain units");
    require(!instance.params->text_to_value(instance.plugin,Input,"nan",&value),"reject nonfinite text");
    instance.set(drive,0);instance.set(tone,5);instance.on();instance.audio(24000);auto quiet=instance.audio(8192);
    instance.set(drive,10);instance.audio(24000);auto hot=instance.audio(8192);require(difference(quiet,hot)>.005,"automation changes the learned audio");
    instance.set(drive,std::numeric_limits<double>::quiet_NaN());require(instance.value(drive)==10,"reject nonfinite automation");
    auto saved=instance.save();const auto document=Json::parse(saved.data);require(document.at("model").at("weights").size()>0,"project state embeds weights");
    Stream bad;bad.data="{}";require(!instance.restore(bad)&&instance.value(drive)==10,"bad state is transactional");
    instance.off();
    Instance recalled(factory);require(recalled.restore(saved),"embedded model state restores without path lookup");require(recalled.find("Drive")==drive&&recalled.value(drive)==10,"stable automation ID and restored value");
    for(double rate:{44100.,48000.,88200.,96000.,192000.}){recalled.on(rate);recalled.audio(4096,nullptr,true,true);recalled.plugin->reset(recalled.plugin);recalled.audio(1024);recalled.off();}
    require(!recalled.plugin->activate(recalled.plugin,0,1,512),"invalid sample rate rejected");
    require(!recalled.plugin->activate(recalled.plugin,48000,0,512),"invalid host block bounds rejected");
    Json dual=readJson(NAM_CLAP_DEMO);
    Json container=readJson(std::filesystem::path(NAM_CLAP_ROOT)/"tests/fixtures/parametric-trainer-a2-dual.json").at("models").at(0).at("model");
    auto combined=document;combined["model"]=container;combined["tier"]=1;combined["controls"]={{"Drive",7},{"Mode",1}};
    Stream combinedState;combinedState.data=combined.dump();require(recalled.restore(combinedState),"combined model restores");
    const auto size=recalled.find("Model size");require(size==Size&&recalled.value(size)==1,"combined starts on Standard");
    require(recalled.params->value_to_text(recalled.plugin,size,0,text,sizeof(text))&&!std::strcmp(text,"Nano"),"Nano size label");
    require(recalled.params->value_to_text(recalled.plugin,size,1,text,sizeof(text))&&!std::strcmp(text,"Standard"),"Standard size label");
    recalled.on();recalled.set(drive,7);recalled.set(size,0);
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    bool switched=false;
    while(std::chrono::steady_clock::now()<limit){recalled.callbacks();auto s=recalled.save();auto j=Json::parse(s.data);if(j["tier"]==0&&!recalled.host.restart.load()){
      // A callback after preparation stages a restart; wait for a real rescan.
      if(recalled.host.rescans>2){switched=true;break;}}
      std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    require(switched&&recalled.value(drive)==7,"tier switch retains live control values");recalled.audio(2048);
    const auto rescans=recalled.host.rescans;
    recalled.set(size,1);recalled.callbacks(); // Begin preparing Standard.
    recalled.set(size,0);recalled.callbacks(); // Keep the later Nano request while busy.
    const auto rapidLimit=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(recalled.host.rescans<rescans+2&&std::chrono::steady_clock::now()<rapidLimit){recalled.callbacks();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    require(recalled.host.rescans>=rescans+2&&recalled.value(size)==0&&recalled.value(drive)==7,"latest size request survives background preparation and host restarts");
    recalled.set(size,1);
    const auto pendingLimit=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(!recalled.host.restart.load()&&std::chrono::steady_clock::now()<pendingLimit){
      if(recalled.host.callback.exchange(false))recalled.plugin->on_main_thread(recalled.plugin);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(recalled.host.restart.load(),"tier preparation reaches host restart boundary");
    recalled.set(drive,8);auto pendingState=recalled.save();
    require(Json::parse(pendingState.data)["controls"]["Drive"]==8,"project save retains edits made while a tier awaits restart");
    recalled.callbacks();require(recalled.value(drive)==8&&recalled.value(size)==1,"tier commit retains the same saved live edits");
    recalled.audio(2048);recalled.off();
    require(recalled.loadFile(NAM_CLAP_SNAPSHOT),"ordinary snapshot model loads");require(recalled.params->count(recalled.plugin)==6,"snapshot has no learned controls or fake tiers");require(recalled.host.clears>=3,"removed controls clear stale host automation");
    auto noMetadata=readJson(NAM_CLAP_SNAPSHOT);noMetadata["metadata"]=nullptr;
    auto snapshotState=document;snapshotState["model"]=noMetadata;snapshotState["tier"]=0;snapshotState["controls"]=Json::object();
    Stream plain;plain.data=snapshotState.dump();require(recalled.restore(plain),"snapshot NAM accepts null optional metadata");
    auto invalid=dual;invalid["architecture"]="HyperWaveNet";combined["model"]=invalid;Stream invalidState;invalidState.data=combined.dump();
    require(!recalled.restore(invalidState)&&recalled.params->count(recalled.plugin)==6,"unsupported model preserves the old model");
    auto info=ModelInfo::parse(dual,"Demo");auto reordered=dual;
    std::reverse(reordered["config"]["params"].begin(),reordered["config"]["params"].end());auto second=ModelInfo::parse(reordered,"Reordered");
    require(info.controls[0].id==second.controls[1].id,"IDs follow definitions instead of parameter order");
    auto absentEnums=dual;absentEnums["config"]["params"][0].erase("enum_names");
    auto nullEnums=absentEnums;nullEnums["config"]["params"][0]["enum_names"]=nullptr;
    require(ModelInfo::parse(absentEnums,"").controls[0].id==ModelInfo::parse(nullEnums,"").controls[0].id,"null and absent unused labels have the same automation identity");
    auto one=container;one["config"]["submodels"]=Json::array({{{"max_value",1},{"model",container["config"]["submodels"][0]["model"]}}});
    combined["model"]=one;combined["tier"]=0;combinedState.data=combined.dump();require(recalled.restore(combinedState)&&recalled.find("Model size")==CLAP_INVALID_ID,"one-tier container is usable without a size selector");
    for(const auto* file:{"parametric-trainer-a2.json","parametric-trainer-a2-dual.json","parametric-trainer-film.json"}) {
      const auto fixture=readJson(std::filesystem::path(NAM_CLAP_ROOT)/"tests/fixtures"/file);
      for(const auto& entry:fixture.at("models")) {
        const auto& model=entry.at("model");const auto cases=entry.at("cases");
        const auto info=ModelInfo::parse(model,"Trainer fixture");
        combined["model"]=model;combined["controls"]=cases[0].at("parameters");combined["tier"]=info.tiers.size()-1;
        combinedState.data=combined.dump();require(recalled.restore(combinedState),"trainer fixture loads through CLAP");
        require(recalled.params->count(recalled.plugin)==6+info.controls.size()+(info.tiers.size()>1?1:0),"all exported controls exposed");
        for(const auto& c:info.controls) {
          require(recalled.find(c.name)==c.id,"model names and IDs reach host");
          if(c.stepped()){require(recalled.params->value_to_text(recalled.plugin,c.id,c.min,text,sizeof(text))&&c.choices[0]==text,"switch labels reach host");}
        }
        recalled.on();recalled.audio(1024);recalled.off();
        for(const auto& audio:cases) {
          ardor::NamProcessor processor;
          require(processor.loadDocument(model,48000,13,audio.value("slimmable_size",1.f),std::nullopt,audio.at("parameters")),"document API loads real trainer export");
          const auto in=fixture.at("input").get<std::vector<float>>();auto out=in;
          processor.processBlock(in.data(),out.data(),in.size());const auto reference=audio.at("output").get<std::vector<float>>();
          float error=0;for(unsigned i=0;i<out.size();++i)error=std::max(error,std::fabs(out[i]-reference[i]));
          require(error<.00005f,"shared document loader matches Python reference inference");
        }
      }
    }
    // Recall the default/empty project while audio is running. The old engine and
    // parameter definitions remain valid until the host performs its restart.
    const auto oldCount=recalled.params->count(recalled.plugin);recalled.on();
    require(recalled.restore(empty)&&recalled.host.restart.load(),"empty recall requests an audio restart");
    require(recalled.params->count(recalled.plugin)==oldCount,"active recall defers parameter changes");
    recalled.audio(256);recalled.callbacks();
    require(recalled.params->count(recalled.plugin)==6,"host restart commits empty state");recalled.off();

    // Compare the complete CLAP path to Ardor's calibrated/normalized processor,
    // accounting for the reported bridge latency and the initial bypass fade.
    auto calibrated=dual;calibrated["metadata"]["input_level_dbu"]=6;calibrated["metadata"]["loudness"]=-15;
    combined=document;combined["model"]=calibrated;combined["tier"]=0;
    combined["controls"]={{"Drive",5},{"Tone",5}};combined["globals"]={0,0,0,0,1,12};
    combinedState.data=combined.dump();require(recalled.restore(combinedState),"calibrated model restores");
    ardor::NamProcessor reference;
    require(reference.loadDocument(calibrated,48000,64,1,12.f,combined["controls"]),"reference calibrated processor");
    std::vector<float> input(8192),expected(8192);
    for(unsigned i=0;i<input.size();++i)input[i]=.05*std::sin(i*.137);
    reference.processBlock(input.data(),expected.data(),input.size());
    recalled.on();const auto actual=recalled.audio(input.size());
    const auto* latency=static_cast<const clap_plugin_latency_t*>(recalled.plugin->get_extension(recalled.plugin,CLAP_EXT_LATENCY));
    const auto delay=latency->get(recalled.plugin);require(delay>0&&delay<1024,"48 kHz bridge reports bounded latency");
    float error=0;for(unsigned i=4096;i<actual.size();++i)error=std::max(error,std::abs(actual[i]-expected[i-delay]));
    require(error<.00001f,"CLAP preserves Ardor input calibration and loudness normalization");recalled.off();
  }
  entry->deinit();clap_test::close(library);std::cout<<"NAM CLAP model loading, automation, tier/state recall and host-rate checks passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
