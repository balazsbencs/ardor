// SPDX-License-Identifier: MIT
#include <clap/clap.h>
#include "Editor.h"
#include "RateAdapter.h"
#include "dsp/DenormalGuard.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <limits>
#include <thread>

namespace ardor::nam_clap {
namespace {
constexpr unsigned slots = 31, firstControl = 7, queueCapacity = 1024;
constexpr float ceiling = .891250938f; // Ardor's fixed -1 dBFS safety ceiling.
constexpr std::size_t stateLimit = 128 * 1024 * 1024;
constexpr const char* pluginId = "org.ardor.nam-loader";
const char* const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,CLAP_PLUGIN_FEATURE_STEREO,nullptr};
const clap_plugin_descriptor_t descriptor{CLAP_VERSION,pluginId,"Ardor NAM","Ardor",
  "https://github.com/balazsbencs/ardor","","https://github.com/balazsbencs/ardor/issues",
  "0.1.0","Snapshot and parametric NAM loader with model-defined controls",features};
const auto& globals=globalControls;
static_assert(std::atomic<double>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
void copyText(char* out, std::size_t size, const std::string& text) {
  if (!size) return;
  auto n = std::min(size-1,text.size());
  if (n < text.size()) while (n && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80) --n;
  std::memcpy(out,text.data(),n); out[n]=0;
}
struct Prepared {
  ModelInfo info;
  std::unique_ptr<NamProcessor> processor;
  std::array<double,slots> values{};
};
struct Result { std::unique_ptr<Prepared> model; std::string error; bool preserve = false; };
struct UiEvent { uint16_t type; uint32_t id; double value; };

class Plugin {
public:
  explicit Plugin(const clap_host_t* host):host_(host) {
    for (unsigned i=0;i<globals.size();++i) values_[i].store(globals[i].initial);
    for (unsigned i=globals.size();i<slots;++i) values_[i].store(0);
    applied_.fill(std::numeric_limits<double>::quiet_NaN());
    api={&descriptor,this,init,destroy,activate,deactivate,start,stop,reset,process,extension,onMain};
  }
  ~Plugin() {
    destroying_.store(true);
#ifdef ARDOR_NAM_COCOA
    editor_.reset();
#endif
    if (worker_.joinable()) worker_.join();
  }
  clap_plugin_t api{};
private:
  const clap_host_t* host_;
  const clap_host_params_t* hostParams_=nullptr;
  const clap_host_state_t* hostState_=nullptr;
  const clap_host_log_t* hostLog_=nullptr;
  std::unique_ptr<Prepared> model_,pending_;
  std::array<double,slots> pendingEmptyValues_{};
  std::array<std::atomic<double>,slots> values_{};
  std::array<double,24> applied_{};
  std::array<UiEvent,queueCapacity> events_{};
  std::atomic<unsigned> write_{0},read_{0};
  std::atomic<int> requestedTier_{-1};
  std::atomic<bool> destroying_{false},fault_{false},callbackQueued_{false};
  std::atomic<float> inputPeak_{0},outputPeak_{0};
  std::future<Result> future_;
  std::thread worker_;
  bool active_=false,loading_=false,pendingPreserve_=false,pendingChange_=false;
  uint32_t gestureId_=CLAP_INVALID_ID;
  uint64_t revision_=1;
  std::string status_="Load a NAM model to begin.";
  clap_audio::RateAdapter rate_;
  double hostRate_=48000;
  uint32_t maxFrames_=0;
  float inputGain_=1,outputGain_=1,mix_=0,smooth_=0;
  double inputTarget_=1,outputTarget_=1,mixTarget_=0;
#ifdef ARDOR_NAM_COCOA
  std::unique_ptr<Editor> editor_;
#endif
  static Plugin& self(const clap_plugin_t* p) {return *static_cast<Plugin*>(p->plugin_data);}
  int slot(uint32_t id) const noexcept {
    if (id>=Input && id<=Reference) return static_cast<int>(id-1);
    if (id==Size && model_ && model_->info.tiers.size()>1) return Size-1;
    if (model_) for (unsigned i=0;i<model_->info.controls.size();++i)
      if (model_->info.controls[i].id==id) return firstControl+i;
    return -1;
  }
  const Control* definition(uint32_t id) const noexcept {
    if (id>=Input && id<=Reference) return &globals[id-1];
    if (model_) for (const auto& c:model_->info.controls) if(c.id==id) return &c;
    return nullptr;
  }
  uint32_t idAt(unsigned index) const {
    if(index<globals.size()) return index+1;
    index-=globals.size();
    if(model_ && model_->info.tiers.size()>1) {if(index==0)return Size;--index;}
    return model_ && index<model_->info.controls.size()?model_->info.controls[index].id:CLAP_INVALID_ID;
  }
  double value(uint32_t id) const {
    if(id==Size) return values_[Size-1].load(std::memory_order_relaxed);
    const int n=slot(id);return n<0?0:values_[n].load(std::memory_order_relaxed);
  }
  void callback() noexcept {
    if(!callbackQueued_.exchange(true) && host_->request_callback) host_->request_callback(host_);
  }
  void fail(std::string error) {
    status_=std::move(error);++revision_;
    if(hostLog_)hostLog_->log(host_,CLAP_LOG_ERROR,status_.c_str());
  }
  static std::unique_ptr<Prepared> prepare(ModelInfo info,int tier,const Json& controls,
                                           const std::array<double,slots>& values) {
    if(tier<0 || static_cast<std::size_t>(tier)>=info.tiers.size()) throw std::runtime_error("Invalid model size");
    auto result=std::make_unique<Prepared>();result->info=std::move(info);result->values=values;
    result->values[Size-1]=tier;
    if(!controls.is_object()) throw std::runtime_error("Model controls must be an object");
    for(auto it=controls.begin();it!=controls.end();++it) {
      auto found=std::find_if(result->info.controls.begin(),result->info.controls.end(),[&](const auto& c){return c.name==it.key();});
      if(found==result->info.controls.end())throw std::runtime_error("Saved control is absent from this model: "+it.key());
    }
    Json targets=Json::object();
    for(unsigned i=0;i<result->info.controls.size();++i) {
      const auto& c=result->info.controls[i];
      const double v=controls.contains(c.name)?controls.at(c.name).get<double>():c.initial;
      if(!std::isfinite(v)||v<c.min||v>c.max||(c.stepped()&&std::round(v)!=v))
        throw std::runtime_error("Invalid saved value for "+c.name);
      result->values[firstControl+i]=v;targets[c.name]=v;
    }
    result->processor=std::make_unique<NamProcessor>();
    if(!result->processor->loadDocument(*result->info.document,48000,64,
        result->info.tiers[tier].size,std::nullopt,targets))
      throw std::runtime_error("Cannot prepare this NAM. Check its weights, architecture and sample rate.");
    return result;
  }
  std::array<double,slots> snapshotValues() const {
    std::array<double,slots> values{};
    for(unsigned i=0;i<slots;++i)values[i]=values_[i].load();
    return values;
  }
  Json controls() const {
    Json result=Json::object();
    if(model_)for(unsigned i=0;i<model_->info.controls.size();++i)
      result[model_->info.controls[i].name]=values_[firstControl+i].load();
    return result;
  }
  void install(std::unique_ptr<Prepared> prepared,bool preserve) {
    const int requested=preserve?requestedTier_.load():-1;
    std::vector<uint32_t> old;
    for(unsigned i=0;i<paramCount(&api);++i)old.push_back(idAt(i));
    if(preserve && prepared && model_)prepared->values=snapshotValues();
    // A tier choice was prepared already; preserve every other live control.
    const auto chosen=prepared?prepared->processor->slimmableSize():1.f;
    if(preserve && prepared) {
      auto tier=std::find_if(prepared->info.tiers.begin(),prepared->info.tiers.end(),[&](const auto& t){return t.size==chosen;});
      prepared->values[Size-1]=std::distance(prepared->info.tiers.begin(),tier);
    }
    model_=std::move(prepared);
    if(model_)for(unsigned i=0;i<slots;++i)values_[i].store(model_->values[i]);
    else for(unsigned i=0;i<slots;++i)values_[i].store(i<globals.size()?pendingEmptyValues_[i]:0);
    read_.store(write_.load());gestureId_=CLAP_INVALID_ID;requestedTier_.store(requested);pendingChange_=false;
    applied_.fill(std::numeric_limits<double>::quiet_NaN());fault_.store(false);
    status_=model_?"Ready":"Load a NAM model to begin.";++revision_;
    if(hostParams_) {
      for(auto id:old)if(slot(id)<0)hostParams_->clear(host_,id,CLAP_PARAM_CLEAR_ALL);
      hostParams_->rescan(host_,CLAP_PARAM_RESCAN_ALL);
    }
    if(requested>=0)callback();
  }
  void stage(std::unique_ptr<Prepared> prepared,bool preserve=false) {
    if(active_) {
      pending_=std::move(prepared);pendingPreserve_=preserve;pendingChange_=true;
      status_="Ready to switch — waiting for the host to restart audio.";++revision_;
      host_->request_restart(host_);
    } else install(std::move(prepared),preserve);
    if(hostState_)hostState_->mark_dirty(host_);
  }
  void asyncLoad(std::filesystem::path path) {
    if(loading_ || pendingChange_)return;
    loading_=true;status_="Loading model…";++revision_;
    const auto values=snapshotValues();
    auto promise=std::make_shared<std::promise<Result>>();future_=promise->get_future();
    try {worker_=std::thread([this,promise,path=std::move(path),values] {
      Result result;
      try {
        auto info=ModelInfo::read(path);
        const auto tier=static_cast<int>(info.tiers.size()-1);
        result.model=prepare(std::move(info),tier,Json::object(),values);
      }catch(const std::exception& e){result.error=e.what();}
      promise->set_value(std::move(result));
      if(!destroying_.load())callback();
    });}catch(...){loading_=false;future_={};throw;}
  }
  void tierLoad(int tier) {
    if(!model_ || loading_ || pendingChange_ || tier<0 || static_cast<std::size_t>(tier)>=model_->info.tiers.size())return;
    if(tier==static_cast<int>(model_->values[Size-1]))return;
    loading_=true;status_="Preparing model size…";++revision_;
    auto info=model_->info;const auto targets=controls();const auto values=snapshotValues();
    auto promise=std::make_shared<std::promise<Result>>();future_=promise->get_future();
    try {worker_=std::thread([this,promise,info=std::move(info),tier,targets,values]() mutable {
      Result result;result.preserve=true;
      try {result.model=prepare(std::move(info),tier,targets,values);}
      catch(const std::exception& e){result.error=e.what();}
      promise->set_value(std::move(result));
      if(!destroying_.load())callback();
    });}catch(...){loading_=false;future_={};throw;}
  }
  void tick() {
    if(future_.valid() && future_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
      auto result=future_.get();if(worker_.joinable())worker_.join();loading_=false;
      if(result.model) {
        if(!result.preserve)for(unsigned i=0;i<globals.size();++i)result.model->values[i]=values_[i].load();
        stage(std::move(result.model),result.preserve);
      }else {
        if(model_)values_[Size-1].store(model_->values[Size-1]);
        fail(result.error);
      }
    }
    if(!loading_&&!pendingChange_) {
      const int tier=requestedTier_.exchange(-1);
      if(tier>=0)tierLoad(tier);
    }
  }
  bool push(UiEvent event) noexcept {
    const auto write=write_.load(std::memory_order_relaxed);
    if(write-read_.load(std::memory_order_acquire)>=queueCapacity)return false;
    events_[write%queueCapacity]=event;write_.store(write+1,std::memory_order_release);return true;
  }
  void gesture(uint32_t id,bool begin) {
    if(slot(id)<0)return;
    // Reserve room for the matching end even when the host stops accepting events.
    if(begin) {
      if(gestureId_!=CLAP_INVALID_ID||write_.load()-read_.load()>=queueCapacity-1)return;
      if(push({CLAP_EVENT_PARAM_GESTURE_BEGIN,id,0}))gestureId_=id;
    }else {
      if(gestureId_!=id)return;
      push({CLAP_EVENT_PARAM_GESTURE_END,id,0});gestureId_=CLAP_INVALID_ID;
    }
    if(hostParams_)hostParams_->request_flush(host_);
  }
  void uiChange(uint32_t id,double v) {
    const int n=slot(id);if(n<0||!std::isfinite(v))return;
    const auto* c=definition(id);
    const double low=id==Size?0:c->min, high=id==Size?static_cast<double>(model_->info.tiers.size()-1):c->max;
    v=std::clamp(v,low,high);if(id==Size||(c&&c->stepped()))v=std::round(v);
    if(gestureId_!=id||write_.load()-read_.load()>=queueCapacity-1||!push({CLAP_EVENT_PARAM_VALUE,id,v}))return;
    values_[n].store(v);
    if(id==Size){requestedTier_.store(static_cast<int>(v));tick();}
    if(hostParams_)hostParams_->request_flush(host_);
  }
  void event(const clap_event_header_t* h) noexcept {
    if(!h||h->space_id!=CLAP_CORE_EVENT_SPACE_ID||h->type!=CLAP_EVENT_PARAM_VALUE||h->size<sizeof(clap_event_param_value_t))return;
    const auto& e=*reinterpret_cast<const clap_event_param_value_t*>(h);
    if(e.note_id!=-1||e.port_index!=-1||e.channel!=-1||e.key!=-1||!std::isfinite(e.value))return;
    const int n=slot(e.param_id);if(n<0)return;
    const auto* c=definition(e.param_id);
    const double low=e.param_id==Size?0:c->min,high=e.param_id==Size?static_cast<double>(model_->info.tiers.size()-1):c->max;
    double v=std::clamp(e.value,low,high);if(e.param_id==Size||(c&&c->stepped()))v=std::round(v);
    values_[n].store(v,std::memory_order_relaxed);
    if(e.param_id==Size){requestedTier_.store(static_cast<int>(v));callback();}
  }
  void emit(const clap_output_events_t* out) noexcept {
    if(!out||!out->try_push)return;
    auto read=read_.load(std::memory_order_relaxed);
    const auto write=write_.load(std::memory_order_acquire);
    while(read!=write) {
      const auto& e=events_[read%queueCapacity];bool accepted;
      if(e.type==CLAP_EVENT_PARAM_VALUE) {
        clap_event_param_value_t v{};v.header={sizeof(v),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};
        v.param_id=e.id;v.value=e.value;v.note_id=-1;v.port_index=v.channel=v.key=-1;
        accepted=out->try_push(out,&v.header);
      }else {
        clap_event_param_gesture_t v{};v.header={sizeof(v),0,CLAP_CORE_EVENT_SPACE_ID,e.type,0};v.param_id=e.id;
        accepted=out->try_push(out,&v.header);
      }
      if(!accepted)break;
      ++read;
    }
    read_.store(read,std::memory_order_release);
  }
  void targets() noexcept {
    double calibration=0;
    if(model_ && model_->info.inputLevel && value(Calibrate)>=.5)
      calibration=std::clamp(value(Reference)-*model_->info.inputLevel,-60.,60.);
    inputTarget_=std::pow(10.,(value(Input)+calibration)/20.);
    outputTarget_=std::pow(10.,value(Output)/20.);
    mixTarget_=model_ && value(Bypass)<.5 && !fault_.load()?1:0;
  }
  static void engine(void* context,const float* in,float* left,float* right,unsigned frames,int) {
    auto& s=*static_cast<Plugin*>(context);
    if(s.model_ && !s.fault_.load()) {
      for(unsigned i=0;i<s.model_->info.controls.size();++i) {
        const double v=s.values_[firstControl+i].load(std::memory_order_relaxed);
        if(v!=s.applied_[i]) {
          s.model_->processor->setParameterTarget(s.model_->info.controls[i].name,static_cast<float>(v));
          s.applied_[i]=v;
        }
      }
      s.model_->processor->processBlock(in,left,frames);
    }else std::copy(in,in+frames,left);
    for(unsigned i=0;i<frames;++i) {
      if(!std::isfinite(left[i])) {left[i]=0;s.fault_.store(true);s.callback();}
      right[i]=left[i];
    }
  }
  static bool CLAP_ABI init(const clap_plugin_t* p) noexcept {
    auto& s=self(p);s.hostParams_=static_cast<const clap_host_params_t*>(s.host_->get_extension(s.host_,CLAP_EXT_PARAMS));
    s.hostState_=static_cast<const clap_host_state_t*>(s.host_->get_extension(s.host_,CLAP_EXT_STATE));
    s.hostLog_=static_cast<const clap_host_log_t*>(s.host_->get_extension(s.host_,CLAP_EXT_LOG));return true;
  }
  static void CLAP_ABI destroy(const clap_plugin_t* p) noexcept {delete &self(p);}
  static bool CLAP_ABI activate(const clap_plugin_t* p,double rate,uint32_t min,uint32_t max) noexcept {
    auto& s=self(p);
    try {
      if(s.active_||!min||max<min||max>1048576||!clap_audio::RateAdapter::supports(rate))return false;
      s.hostRate_=rate;s.maxFrames_=max;s.rate_.prepare(rate,0);s.rate_.reset(0);
      s.smooth_=static_cast<float>(1-std::exp(-1/(.005*rate)));
      s.targets();s.inputGain_=s.inputTarget_;s.outputGain_=s.outputTarget_;s.mix_=0;
      s.active_=true;return true;
    }catch(...){return false;}
  }
  static void CLAP_ABI deactivate(const clap_plugin_t* p) noexcept {
    auto& s=self(p);s.active_=false;
    try{if(s.pendingChange_)s.install(std::move(s.pending_),s.pendingPreserve_);}catch(...){s.fail("Cannot switch model.");}
  }
  static bool CLAP_ABI start(const clap_plugin_t* p) noexcept {return self(p).active_;}
  static void CLAP_ABI stop(const clap_plugin_t*) noexcept {}
  static void CLAP_ABI reset(const clap_plugin_t* p) noexcept {
    auto& s=self(p);s.rate_.reset(0);if(s.model_)s.model_->processor->reset();
    s.applied_.fill(std::numeric_limits<double>::quiet_NaN());s.targets();
    s.inputGain_=s.inputTarget_;s.outputGain_=s.outputTarget_;s.mix_=0;
  }
  static clap_process_status CLAP_ABI process(const clap_plugin_t* p,const clap_process_t* data) noexcept {
    auto& s=self(p);
    if(!data||!s.active_||data->frames_count>s.maxFrames_||data->audio_inputs_count!=1||data->audio_outputs_count!=1)return CLAP_PROCESS_ERROR;
    const auto& in=data->audio_inputs[0];auto& out=data->audio_outputs[0];
    if(in.channel_count!=2||out.channel_count!=2||(!in.data32&&!in.data64)||(!out.data32&&!out.data64))return CLAP_PROCESS_ERROR;
    ScopedDenormalGuard guard;
    try {
      s.emit(data->out_events);s.targets();
      unsigned e=0,n=data->in_events?data->in_events->size(data->in_events):0;
      float inputPeak=0,outputPeak=0;out.constant_mask=0;
      for(unsigned i=0;i<data->frames_count;++i) {
        bool changed=false;
        while(e<n) {const auto* event=data->in_events->get(data->in_events,e);if(event&&event->time>i)break;s.event(event);++e;changed=true;}
        if(changed)s.targets();
        float l=in.data32?in.data32[0][i]:static_cast<float>(in.data64[0][i]);
        float r=in.data32?in.data32[1][i]:static_cast<float>(in.data64[1][i]);
        if(!std::isfinite(l))l=0;if(!std::isfinite(r))r=0;
        const int channel=static_cast<int>(s.value(Channel));
        const float dry=channel==0?l:channel==1?r:l*.5f+r*.5f;
        inputPeak=std::max(inputPeak,std::fabs(dry));
        s.inputGain_+=s.smooth_*(s.inputTarget_-s.inputGain_);
        s.outputGain_+=s.smooth_*(s.outputTarget_-s.outputGain_);
        s.mix_+=s.smooth_*(s.mixTarget_-s.mix_);
        clap_audio::RateAdapter::Output sample{};
        if(!s.rate_.tick(dry*s.inputGain_,dry,0,engine,&s,sample))return CLAP_PROCESS_ERROR;
        float wet=sample.left*s.outputGain_;
        if(!std::isfinite(wet)){wet=0;s.fault_.store(true);s.callback();}
        float output=sample.dry+(wet-sample.dry)*s.mix_;
        output=std::isfinite(output)?std::clamp(output,-ceiling,ceiling):0;
        outputPeak=std::max(outputPeak,std::fabs(output));
        if(out.data32)out.data32[0][i]=out.data32[1][i]=output;
        else out.data64[0][i]=out.data64[1][i]=output;
      }
      s.inputPeak_.store(std::max(s.inputPeak_.load(),inputPeak));
      s.outputPeak_.store(std::max(s.outputPeak_.load(),outputPeak));return CLAP_PROCESS_CONTINUE;
    }catch(...){
      if(out.data32)for(unsigned c=0;c<2;++c)std::fill(out.data32[c],out.data32[c]+data->frames_count,0.f);
      else for(unsigned c=0;c<2;++c)std::fill(out.data64[c],out.data64[c]+data->frames_count,0.);
      s.fault_.store(true);s.callback();return CLAP_PROCESS_ERROR;
    }
  }
  static void CLAP_ABI onMain(const clap_plugin_t* p) noexcept {
    auto& s=self(p);s.callbackQueued_.store(false);
    try{s.tick();if(s.fault_.load())s.fail("Model produced non-finite audio. Bypass it or load another model.");}catch(const std::exception& e){s.fail(e.what());}
  }
  static uint32_t CLAP_ABI paramCount(const clap_plugin_t* p) noexcept {
    const auto& m=self(p).model_;return globals.size()+(m?m->info.controls.size()+(m->info.tiers.size()>1?1:0):0);
  }
  static bool CLAP_ABI paramInfo(const clap_plugin_t* p,uint32_t index,clap_param_info_t* out) noexcept {
    if(!out||index>=paramCount(p))return false;
    const auto& s=self(p);const auto id=s.idAt(index);*out={};out->id=id;
    if(id==Size) {
      out->flags=CLAP_PARAM_IS_STEPPED|CLAP_PARAM_IS_ENUM;
      copyText(out->name,sizeof(out->name),"Model size");out->min_value=0;
      out->max_value=s.model_->info.tiers.size()-1;out->default_value=out->max_value;return true;
    }
    const auto* c=s.definition(id);if(!c)return false;
    out->flags=CLAP_PARAM_IS_AUTOMATABLE;
    if(c->stepped())out->flags|=CLAP_PARAM_IS_STEPPED|CLAP_PARAM_IS_ENUM;
    if(id==Bypass)out->flags|=CLAP_PARAM_IS_BYPASS;
    copyText(out->name,sizeof(out->name),c->name);
    copyText(out->module,sizeof(out->module),id>=0x10000000?"Model":"Input and output");
    out->min_value=c->min;out->max_value=c->max;out->default_value=c->initial;return true;
  }
  static bool CLAP_ABI getValue(const clap_plugin_t* p,clap_id id,double* out) noexcept {
    if(!out||self(p).slot(id)<0)return false;*out=self(p).value(id);return true;
  }
  static bool CLAP_ABI toText(const clap_plugin_t* p,clap_id id,double value,char* out,uint32_t size) noexcept {
    if(!out||!size||!std::isfinite(value)||self(p).slot(id)<0)return false;
    const auto& s=self(p);
    if(id==Size) {const int i=static_cast<int>(std::clamp(std::round(value),0.,static_cast<double>(s.model_->info.tiers.size()-1)));copyText(out,size,s.model_->info.tiers[i].name);return true;}
    const auto* c=s.definition(id);if(!c)return false;
    if(c->stepped())copyText(out,size,c->choices[static_cast<std::size_t>(std::clamp(std::round(value-c->min),0.,static_cast<double>(c->choices.size()-1)))]);
    else std::snprintf(out,size,id==Input||id==Output?"%.2f dB":id==Reference?"%.2f dBu":"%.3f",value);
    return true;
  }
  static bool CLAP_ABI fromText(const clap_plugin_t* p,clap_id id,const char* text,double* out) noexcept {
    if(!text||!out||self(p).slot(id)<0)return false;
    const auto& s=self(p);const auto* c=s.definition(id);
    if(id==Size) {for(unsigned i=0;i<s.model_->info.tiers.size();++i)if(s.model_->info.tiers[i].name==text){*out=i;return true;}return false;}
    for(unsigned i=0;i<c->choices.size();++i)if(c->choices[i]==text){*out=c->min+i;return true;}
    char* end=nullptr;double v=std::strtod(text,&end);if(end==text||!std::isfinite(v))return false;
    while(*end==' ')++end;
    if(*end&&!(id==Reference&&std::strcmp(end,"dBu")==0)&&!((id==Input||id==Output)&&std::strcmp(end,"dB")==0))return false;
    if(v<c->min||v>c->max||(c->stepped()&&v!=std::round(v)))return false;*out=v;return true;
  }
  static void CLAP_ABI flush(const clap_plugin_t* p,const clap_input_events_t* in,const clap_output_events_t* out) noexcept {
    auto& s=self(p);if(in)for(unsigned i=0,n=in->size(in);i<n;++i)s.event(in->get(in,i));s.emit(out);
  }
  Json stateJson() const {
    const auto* m=pendingChange_?pending_.get():model_.get();
    Json g=Json::array();for(unsigned i=0;i<globals.size();++i)g.push_back(pendingChange_?(m?m->values[i]:pendingEmptyValues_[i]):values_[i].load());
    Json values=Json::object();
    if(m)for(unsigned i=0;i<m->info.controls.size();++i)values[m->info.controls[i].name]=pendingChange_?m->values[firstControl+i]:values_[firstControl+i].load();
    return {{"version",1},{"model",m?*m->info.document:Json{}},{"name",m?m->info.name:""},
      {"tier",m?static_cast<int>(pendingChange_?m->values[Size-1]:values_[Size-1].load()):0},{"globals",g},{"controls",values}};
  }
  static bool CLAP_ABI save(const clap_plugin_t* p,const clap_ostream_t* stream) noexcept {
    try {
      if(!stream||!stream->write)return false;const auto data=self(p).stateJson().dump();if(data.size()>stateLimit)return false;
      std::size_t offset=0;
      while(offset<data.size()){const auto n=stream->write(stream,data.data()+offset,data.size()-offset);if(n<=0||static_cast<uint64_t>(n)>data.size()-offset)return false;offset+=n;}return true;
    }catch(...){return false;}
  }
  static bool CLAP_ABI load(const clap_plugin_t* p,const clap_istream_t* stream) noexcept {
    auto& s=self(p);
    try {
      if(!stream||!stream->read||s.loading_)return false;
      std::string text;std::array<char,16384> buffer{};
      for(;;){const auto n=stream->read(stream,buffer.data(),buffer.size());if(n<0||n>static_cast<int64_t>(buffer.size()))return false;if(!n)break;if(text.size()+n>stateLimit)return false;text.append(buffer.data(),n);}
      const auto j=Json::parse(text);if(j.at("version")!=1||!j.at("globals").is_array()||j.at("globals").size()!=globals.size())return false;
      auto values=s.snapshotValues();
      for(unsigned i=0;i<globals.size();++i) {
        const double v=j.at("globals")[i].get<double>();const auto& c=globals[i];
        if(!std::isfinite(v)||v<c.min||v>c.max||(c.stepped()&&std::round(v)!=v))return false;values[i]=v;
      }
      if(j.at("model").is_null()) {
        s.pendingEmptyValues_=values;s.stage(nullptr);return true;
      }
      auto info=ModelInfo::parse(j.at("model"),j.at("name").get<std::string>());
      auto model=prepare(std::move(info),j.at("tier").get<int>(),j.at("controls"),values);
      s.stage(std::move(model));return true;
    }catch(const std::exception& e){s.fail(e.what());return false;}
  }
  static bool CLAP_ABI preset(const clap_plugin_t* p,uint32_t kind,const char* path,const char* key) noexcept {
    auto& s=self(p);
    try {
      if(kind!=CLAP_PRESET_DISCOVERY_LOCATION_FILE||!path||(key&&*key)||s.loading_)return false;
      auto info=ModelInfo::read(std::filesystem::u8path(path));const int tier=static_cast<int>(info.tiers.size()-1);
      s.stage(prepare(std::move(info),tier,Json::object(),s.snapshotValues()));return true;
    }catch(const std::exception& e){s.fail(e.what());return false;}
  }
  static uint32_t CLAP_ABI portCount(const clap_plugin_t*,bool) noexcept{return 1;}
  static bool CLAP_ABI portInfo(const clap_plugin_t*,uint32_t index,bool input,clap_audio_port_info_t* out) noexcept {
    if(index||!out)return false;*out={};out->id=0;out->flags=CLAP_AUDIO_PORT_IS_MAIN|CLAP_AUDIO_PORT_SUPPORTS_64BITS;
    out->channel_count=2;out->port_type=CLAP_PORT_STEREO;out->in_place_pair=0;
    copyText(out->name,sizeof(out->name),input?"Guitar input":"NAM output");return true;
  }
  static uint32_t CLAP_ABI latency(const clap_plugin_t* p) noexcept{return self(p).rate_.latency();}
  static uint32_t CLAP_ABI tail(const clap_plugin_t*) noexcept{return INT32_MAX;}
  static bool CLAP_ABI hardRealtime(const clap_plugin_t*) noexcept{return false;}
  static bool CLAP_ABI render(const clap_plugin_t*,clap_plugin_render_mode mode) noexcept{return mode==CLAP_RENDER_REALTIME||mode==CLAP_RENDER_OFFLINE;}
#ifdef ARDOR_NAM_COCOA
  static bool CLAP_ABI guiSupported(const clap_plugin_t*,const char* api,bool floating) noexcept{return api&&!floating&&std::strcmp(api,CLAP_WINDOW_API_COCOA)==0;}
  static bool CLAP_ABI guiPreferred(const clap_plugin_t*,const char** api,bool* floating) noexcept{if(!api||!floating)return false;*api=CLAP_WINDOW_API_COCOA;*floating=false;return true;}
  static bool CLAP_ABI guiCreate(const clap_plugin_t* p,const char* api,bool floating) noexcept {
    auto& s=self(p);if(!guiSupported(p,api,floating)||s.editor_)return false;
    try {
      s.editor_=createEditor({[&s]{return EditorState{s.model_?&s.model_->info:nullptr,s.status_,s.revision_,s.loading_||s.pendingChange_,s.inputPeak_.exchange(0),s.outputPeak_.exchange(0)};},
        [&s](uint32_t id){return s.value(id);},[&s](uint32_t id,double v){s.uiChange(id,v);},
        [&s](uint32_t id,bool begin){s.gesture(id,begin);},[&s](auto path){s.asyncLoad(std::move(path));},[&s]{s.tick();}});
      return static_cast<bool>(s.editor_);
    }catch(...){return false;}
  }
  static void CLAP_ABI guiDestroy(const clap_plugin_t* p) noexcept{self(p).editor_.reset();}
  static bool CLAP_ABI guiScale(const clap_plugin_t*,double) noexcept{return false;}
  static bool CLAP_ABI guiSize(const clap_plugin_t* p,uint32_t* w,uint32_t* h) noexcept{if(!w||!h||!self(p).editor_)return false;*w=self(p).editor_->width();*h=self(p).editor_->height();return true;}
  static bool CLAP_ABI guiResizable(const clap_plugin_t*) noexcept{return true;}
  static bool CLAP_ABI guiHints(const clap_plugin_t*,clap_gui_resize_hints_t* h) noexcept{if(!h)return false;*h={true,true,false,0,0};return true;}
  static bool CLAP_ABI guiAdjust(const clap_plugin_t*,uint32_t* w,uint32_t* h) noexcept{if(!w||!h)return false;*w=std::clamp(*w,600u,1400u);*h=std::clamp(*h,600u,1200u);return true;}
  static bool CLAP_ABI guiResize(const clap_plugin_t* p,uint32_t w,uint32_t h) noexcept{return self(p).editor_&&w>=600&&w<=1400&&h>=600&&h<=1200&&self(p).editor_->resize(w,h);}
  static bool CLAP_ABI guiParent(const clap_plugin_t* p,const clap_window_t* w) noexcept{return w&&w->api&&std::strcmp(w->api,CLAP_WINDOW_API_COCOA)==0&&self(p).editor_&&self(p).editor_->parent(w->cocoa);}
  static bool CLAP_ABI guiTransient(const clap_plugin_t*,const clap_window_t*) noexcept{return false;}
  static void CLAP_ABI guiTitle(const clap_plugin_t*,const char*) noexcept{}
  static bool CLAP_ABI guiShow(const clap_plugin_t* p) noexcept{return self(p).editor_&&self(p).editor_->show(true);}
  static bool CLAP_ABI guiHide(const clap_plugin_t* p) noexcept{return self(p).editor_&&self(p).editor_->show(false);}
#endif
  static const void* CLAP_ABI extension(const clap_plugin_t*,const char* id) noexcept {
    if(!id)return nullptr;
    static const clap_plugin_params_t params{paramCount,paramInfo,getValue,toText,fromText,flush};
    static const clap_plugin_state_t state{save,load};static const clap_plugin_audio_ports_t ports{portCount,portInfo};
    static const clap_plugin_latency_t latencyExtension{latency};static const clap_plugin_tail_t tailExtension{tail};
    static const clap_plugin_render_t renderExtension{hardRealtime,render};static const clap_plugin_preset_load_t presets{preset};
#ifdef ARDOR_NAM_COCOA
    static const clap_plugin_gui_t gui{guiSupported,guiPreferred,guiCreate,guiDestroy,guiScale,guiSize,guiResizable,guiHints,guiAdjust,guiResize,guiParent,guiTransient,guiTitle,guiShow,guiHide};
    if(std::strcmp(id,CLAP_EXT_GUI)==0)return &gui;
#endif
    if(std::strcmp(id,CLAP_EXT_PARAMS)==0)return &params;if(std::strcmp(id,CLAP_EXT_STATE)==0)return &state;
    if(std::strcmp(id,CLAP_EXT_AUDIO_PORTS)==0)return &ports;if(std::strcmp(id,CLAP_EXT_LATENCY)==0)return &latencyExtension;
    if(std::strcmp(id,CLAP_EXT_TAIL)==0)return &tailExtension;if(std::strcmp(id,CLAP_EXT_RENDER)==0)return &renderExtension;
    if(std::strcmp(id,CLAP_EXT_PRESET_LOAD)==0)return &presets;return nullptr;
  }
};
uint32_t CLAP_ABI count(const clap_plugin_factory_t*) noexcept{return 1;}
const clap_plugin_descriptor_t* CLAP_ABI describe(const clap_plugin_factory_t*,uint32_t i) noexcept{return i?nullptr:&descriptor;}
const clap_plugin_t* CLAP_ABI create(const clap_plugin_factory_t*,const clap_host_t* host,const char* id) noexcept {
  if(!host||!id||std::strcmp(id,pluginId)||!clap_version_is_compatible(host->clap_version))return nullptr;
  try{return &(new Plugin(host))->api;}catch(...){return nullptr;}
}
bool CLAP_ABI entryInit(const char*) noexcept{return true;}
void CLAP_ABI entryDeinit() noexcept{}
const void* CLAP_ABI factory(const char* id) noexcept {
  static const clap_plugin_factory_t f{count,describe,create};return id&&std::strcmp(id,CLAP_PLUGIN_FACTORY_ID)==0?&f:nullptr;
}
} // namespace
} // namespace ardor::nam_clap
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{CLAP_VERSION,
  ardor::nam_clap::entryInit,ardor::nam_clap::entryDeinit,ardor::nam_clap::factory};
