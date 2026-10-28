// SPDX-License-Identifier: MIT
#pragma once
#include <clap/clap.h>
#include "ModelInfo.h"
#include "ClapTestPlatform.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <cstdlib>
#include <new>

inline thread_local bool inAudioCallback = false;
inline std::atomic<std::size_t> audioAllocations{0};
void* operator new(std::size_t size) {
  if(inAudioCallback)audioAllocations.fetch_add(1);
  if(auto* p=std::malloc(size?size:1))return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}

using namespace ardor::nam_clap;
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
struct Events {
  std::vector<clap_event_param_value_t> events;
  clap_input_events_t api{this,[](const clap_input_events_t* in){return static_cast<uint32_t>(static_cast<Events*>(in->ctx)->events.size());},
    [](const clap_input_events_t* in,uint32_t i)->const clap_event_header_t*{return &static_cast<Events*>(in->ctx)->events.at(i).header;}};
  void add(clap_id id,double value,uint32_t time=0){clap_event_param_value_t e{};e.header={sizeof(e),time,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};
    e.param_id=id;e.value=value;e.note_id=-1;e.port_index=e.channel=e.key=-1;events.push_back(e);}
};
struct Stream {
  std::string data;std::size_t position=0;
  Stream()=default;
  Stream(Stream&& other):data(std::move(other.data)),position(other.position){}
  clap_ostream_t out{this,[](const clap_ostream_t* s,const void* bytes,uint64_t count)->int64_t{
    auto& me=*static_cast<Stream*>(s->ctx);const auto n=std::min<uint64_t>(count,17);me.data.append(static_cast<const char*>(bytes),n);return n;}};
  clap_istream_t in{this,[](const clap_istream_t* s,void* bytes,uint64_t count)->int64_t{
    auto& me=*static_cast<Stream*>(s->ctx);const auto n=std::min<std::size_t>({count,me.data.size()-me.position,31});
    std::memcpy(bytes,me.data.data()+me.position,n);me.position+=n;return n;}};
};
struct Host {
  std::atomic<bool> callback{false},restart{false};bool active=false;unsigned rescans=0,clears=0;std::vector<std::string> errors;
  clap_host_t api{CLAP_VERSION,this,"NAM tests","Ardor","", "1",
    [](const clap_host_t* h,const char* id)->const void*{
      static const clap_host_params_t params{
        [](const clap_host_t* h,clap_param_rescan_flags flags){auto& me=*static_cast<Host*>(h->host_data);if(flags&CLAP_PARAM_RESCAN_ALL)require(!me.active,"parameter layout only changes while inactive");++me.rescans;},
        [](const clap_host_t* h,clap_id,clap_param_clear_flags){++static_cast<Host*>(h->host_data)->clears;},[](const clap_host_t*){}};
      static const clap_host_state_t state{[](const clap_host_t*){}};
      static const clap_host_log_t log{[](const clap_host_t* h,clap_log_severity,const char* text){static_cast<Host*>(h->host_data)->errors.emplace_back(text);}};
      if(!std::strcmp(id,CLAP_EXT_PARAMS))return &params;if(!std::strcmp(id,CLAP_EXT_STATE))return &state;if(!std::strcmp(id,CLAP_EXT_LOG))return &log;return nullptr;},
    [](const clap_host_t* h){static_cast<Host*>(h->host_data)->restart.store(true);},[](const clap_host_t*){},
    [](const clap_host_t* h){static_cast<Host*>(h->host_data)->callback.store(true);}};
};
struct Instance {
  Host host;
  const clap_plugin_t* plugin;
  const clap_plugin_params_t* params;
  const clap_plugin_state_t* state;
  const clap_plugin_preset_load_t* preset;
  double rate=48000;
  explicit Instance(const clap_plugin_factory_t* factory) {
    plugin=factory->create_plugin(factory,&host.api,"org.ardor.nam-loader");require(plugin&&plugin->init(plugin),"create and initialize");
    params=static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin,CLAP_EXT_PARAMS));
    state=static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin,CLAP_EXT_STATE));
    preset=static_cast<const clap_plugin_preset_load_t*>(plugin->get_extension(plugin,CLAP_EXT_PRESET_LOAD));
    require(params&&state&&preset,"required extensions");
  }
  ~Instance(){off();plugin->destroy(plugin);}
  void on(double r=48000){rate=r;require(plugin->activate(plugin,r,1,512),"activate");host.active=true;require(plugin->start_processing(plugin),"start processing");}
  void off(){if(host.active){plugin->stop_processing(plugin);host.active=false;plugin->deactivate(plugin);}}
  bool loadFile(const std::filesystem::path& path){const auto text=clap_test::utf8(path);return preset->from_location(plugin,CLAP_PRESET_DISCOVERY_LOCATION_FILE,text.c_str(),nullptr);}
  double value(clap_id id){double v=0;require(params->get_value(plugin,id,&v),"parameter value");return v;}
  clap_id find(const std::string& name){for(unsigned i=0;i<params->count(plugin);++i){clap_param_info_t info{};require(params->get_info(plugin,i,&info),"get parameter definition");if(name==info.name)return info.id;}return CLAP_INVALID_ID;}
  void set(clap_id id,double value){Events e;e.add(id,value);params->flush(plugin,&e.api,nullptr);}
  Stream save(){Stream s;require(state->save(plugin,&s.out),"save state with partial writes");return s;}
  bool restore(Stream& s){s.position=0;return state->load(plugin,&s.in);}
  std::vector<float> audio(unsigned count,Events* events=nullptr,bool doubles=false,bool inPlace=false){
    require(host.active,"audio requires activation");std::vector<float> all;all.reserve(count);
    const std::array<unsigned,5> sizes{1,17,64,127,257};unsigned position=0,block=0;
    while(position<count){const unsigned n=std::min(sizes[block++%sizes.size()],count-position);
      std::array<float,512> l{},r{},ol{},orr{};std::array<double,512> dl{},dr{},dol{},dor{};
      for(unsigned i=0;i<n;++i){const auto x=.05*std::sin((position+i)*.137);l[i]=dl[i]=x;r[i]=dr[i]=x*.5;}
      float* in32[]={l.data(),r.data()};float* out32[]={inPlace?l.data():ol.data(),inPlace?r.data():orr.data()};
      double* in64[]={dl.data(),dr.data()};double* out64[]={inPlace?dl.data():dol.data(),inPlace?dr.data():dor.data()};
      clap_audio_buffer_t in{doubles?nullptr:in32,doubles?in64:nullptr,2,0,0};
      clap_audio_buffer_t out{doubles?nullptr:out32,doubles?out64:nullptr,2,0,0};
      clap_process_t process{};process.frames_count=n;process.audio_inputs=&in;process.audio_inputs_count=1;
      process.audio_outputs=&out;process.audio_outputs_count=1;process.in_events=position==0&&events?&events->api:nullptr;
      const auto before=audioAllocations.load();inAudioCallback=true;
      const auto result=plugin->process(plugin,&process);inAudioCallback=false;
      require(result==CLAP_PROCESS_CONTINUE,"process mixed block sizes");
      require(audioAllocations.load()==before,"audio callback must not allocate C++ objects");
      for(unsigned i=0;i<n;++i){const double a=doubles?out64[0][i]:out32[0][i],b=doubles?out64[1][i]:out32[1][i];
        require(std::isfinite(a)&&std::fabs(a)<=.891251&&a==b,"finite bounded stereo output");all.push_back(a);}
      position+=n;
    }
    return all;
  }
  void callbacks(){if(host.callback.exchange(false))plugin->on_main_thread(plugin);if(host.restart.exchange(false)){const bool was=host.active;off();if(was)on(rate);}}
};
float difference(const std::vector<float>& a,const std::vector<float>& b){float sum=0;for(unsigned i=0;i<a.size();++i)sum+=std::abs(a[i]-b[i]);return sum/a.size();}
Json readJson(const std::filesystem::path& p){std::ifstream stream(p);return Json::parse(stream);}

