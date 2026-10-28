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
    require(instance.loadFile(NAM_CLAP_DEMO),"load real parametric model");require(instance.params->count(instance.plugin)==8,"model controls exposed");
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
    require(switched&&recalled.value(drive)==7,"tier switch retains live control values");recalled.audio(2048);recalled.off();
    require(recalled.loadFile(NAM_CLAP_SNAPSHOT),"ordinary snapshot model loads");require(recalled.params->count(recalled.plugin)==6,"snapshot has no learned controls or fake tiers");require(recalled.host.clears>=3,"removed controls clear stale host automation");
    auto invalid=dual;invalid["architecture"]="HyperWaveNet";combined["model"]=invalid;Stream invalidState;invalidState.data=combined.dump();
    require(!recalled.restore(invalidState)&&recalled.params->count(recalled.plugin)==6,"unsupported model preserves the old model");
    auto info=ModelInfo::parse(dual,"Demo");auto reordered=dual;
    std::reverse(reordered["config"]["params"].begin(),reordered["config"]["params"].end());auto second=ModelInfo::parse(reordered,"Reordered");
    require(info.controls[0].id==second.controls[1].id,"IDs follow definitions instead of parameter order");
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
  }
  entry->deinit();clap_test::close(library);std::cout<<"NAM CLAP model loading, automation, tier/state recall and host-rate checks passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
