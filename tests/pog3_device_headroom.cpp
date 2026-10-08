// Temporary device diagnostic: real ALSA pacing with deterministic DSP input.
// Writes silence to hardware; all measured output is consumed by a checksum.
#include "audio/EngineLoader.h"
#include "daisyfx/pog3/Pog3Processor.h"
#include "dsp/DenormalGuard.h"
#include <alsa/asoundlib.h>
#include <fftw3.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
std::uint64_t ns(clockid_t clock = CLOCK_MONOTONIC) {
  timespec t{}; clock_gettime(clock, &t); return t.tv_sec * 1000000000ULL + t.tv_nsec;
}
struct Counters {
  std::vector<int> fds;
  std::vector<std::string> names;
  void add(const char* name, unsigned type, std::uint64_t config) {
    perf_event_attr a{}; a.size=sizeof(a); a.type=type; a.config=config;
    a.disabled=1; a.exclude_kernel=1; a.exclude_hv=1;
    a.read_format=PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
    int fd=static_cast<int>(syscall(SYS_perf_event_open,&a,0,-1,-1,0));
    require(fd>=0,std::string("perf ")+name+": "+std::strerror(errno));
    fds.push_back(fd); names.emplace_back(name);
  }
  void begin() { for (int fd:fds) { require(ioctl(fd,PERF_EVENT_IOC_RESET,0)==0,"perf reset"); require(ioctl(fd,PERF_EVENT_IOC_ENABLE,0)==0,"perf enable"); } }
  void end(const std::string& path) {
    for (int fd:fds) require(ioctl(fd,PERF_EVENT_IOC_DISABLE,0)==0,"perf disable");
    std::ofstream out(path); out<<"event,value,enabled_ns,running_ns\n";
    for (std::size_t i=0;i<fds.size();++i) { std::uint64_t data[3]{}; require(read(fds[i],data,sizeof(data))==sizeof(data),"perf read"); out<<names[i]<<','<<data[0]<<','<<data[1]<<','<<data[2]<<'\n'; }
    require(bool(out),"counter output");
  }
  ~Counters(){for(int fd:fds)close(fd);}
};
struct Samples {
  int fd=-1; void* mapping=MAP_FAILED; std::size_t bytes=0, page=0;
  void prepare() {
    perf_event_attr a{}; a.size=sizeof(a); a.type=PERF_TYPE_HARDWARE; a.config=PERF_COUNT_HW_CPU_CYCLES;
    a.disabled=1; a.exclude_kernel=1; a.exclude_hv=1; a.sample_period=1000003;
    a.sample_type=PERF_SAMPLE_IP|PERF_SAMPLE_TIME; a.use_clockid=1; a.clockid=CLOCK_MONOTONIC; a.wakeup_events=0;
    fd=static_cast<int>(syscall(SYS_perf_event_open,&a,0,-1,-1,0));
    require(fd>=0,std::string("perf sample: ")+std::strerror(errno));
    page=static_cast<std::size_t>(sysconf(_SC_PAGESIZE)); bytes=page*257;
    mapping=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    require(mapping!=MAP_FAILED,"perf mmap");
  }
  void begin(){require(ioctl(fd,PERF_EVENT_IOC_ENABLE,0)==0,"sample enable");}
  void end(const std::string& path){
    require(ioctl(fd,PERF_EVENT_IOC_DISABLE,0)==0,"sample disable");
    auto* meta=static_cast<perf_event_mmap_page*>(mapping);
    const auto head=__atomic_load_n(&meta->data_head,__ATOMIC_ACQUIRE);
    const std::size_t size=meta->data_size, offset=meta->data_offset;
    require(size && offset+size<=bytes && head<=size,"sample ring overflow/layout");
    auto* data=static_cast<char*>(mapping)+offset;
    std::ofstream out(path);out<<"ip,time_ns\n";std::uint64_t lost=0, cursor=0, hits=0;
    while(cursor<head){
      perf_event_header h{}; std::memcpy(&h,data+cursor,sizeof(h));
      require(h.size>=sizeof(h) && cursor+h.size<=head,"sample record bounds");
      if(h.type==PERF_RECORD_SAMPLE){require(h.size>=24,"sample size"); std::uint64_t ip,time;std::memcpy(&ip,data+cursor+8,8);std::memcpy(&time,data+cursor+16,8);out<<std::hex<<ip<<std::dec<<','<<time<<'\n';++hits;}
      if(h.type==PERF_RECORD_LOST){require(h.size>=24,"lost size");std::uint64_t n;std::memcpy(&n,data+cursor+16,8);lost+=n;}
      cursor+=h.size;
    }
    std::cerr<<"samples="<<hits<<'\n';
    std::cerr<<"sample_lost="<<lost<<" ring_bytes="<<head<<'\n';require(lost==0 && bool(out),"lost samples/output");
  }
  ~Samples(){if(mapping!=MAP_FAILED)munmap(mapping,bytes);if(fd>=0)close(fd);}
};
struct Pcm {
  snd_pcm_t* handle=nullptr;
  void open(snd_pcm_stream_t stream,unsigned block){
    int result=snd_pcm_open(&handle,"hw:Zero,0",stream,0);require(result>=0,snd_strerror(result));
    snd_pcm_hw_params_t* p; snd_pcm_hw_params_alloca(&p);
    require(snd_pcm_hw_params_any(handle,p)>=0,"ALSA any");
    require(snd_pcm_hw_params_set_access(handle,p,SND_PCM_ACCESS_RW_INTERLEAVED)>=0,"ALSA access");
    require(snd_pcm_hw_params_set_format(handle,p,SND_PCM_FORMAT_S32_LE)>=0,"ALSA format");
    require(snd_pcm_hw_params_set_channels(handle,p,2)>=0,"ALSA channels");
    require(snd_pcm_hw_params_set_rate(handle,p,48000,0)>=0,"ALSA rate");
    require(snd_pcm_hw_params_set_period_size(handle,p,block,0)>=0,"ALSA period");
    require(snd_pcm_hw_params_set_buffer_size(handle,p,3*block)>=0,"ALSA buffer");
    require(snd_pcm_hw_params(handle,p)>=0,"ALSA commit");
    snd_pcm_sw_params_t* sw;snd_pcm_sw_params_alloca(&sw);
    require(snd_pcm_sw_params_current(handle,sw)>=0,"ALSA sw");
    require(snd_pcm_sw_params_set_avail_min(handle,sw,block)>=0,"ALSA avail");
    require(snd_pcm_sw_params_set_start_threshold(handle,sw,stream==SND_PCM_STREAM_PLAYBACK?3*block:1)>=0,"ALSA start threshold");
    require(snd_pcm_sw_params(handle,sw)>=0,"ALSA sw commit");
    require(snd_pcm_prepare(handle)>=0,"ALSA prepare");
  }
  unsigned transfer(std::int32_t* buffer,unsigned frames,bool capture){
    unsigned done=0,xruns=0;
    while(done<frames){
      const auto n=capture?snd_pcm_readi(handle,buffer+2*done,frames-done):snd_pcm_writei(handle,buffer+2*done,frames-done);
      if(n==-EINTR)continue;
      // Stop on the first xrun. Codec prepare/recovery can sleep for a second,
      // causing the opposite stream to xrun and obscuring the original failure.
      if(n==-EPIPE)return 1;
      if(n<=0) throw std::runtime_error(std::string("ALSA transfer: ")+snd_strerror(static_cast<int>(n)));
      done+=static_cast<unsigned>(n);
    }
    return xruns;
  }
  ~Pcm(){if(handle){snd_pcm_drop(handle);snd_pcm_close(handle);}}
};
struct Row {double wall=0,cpu=0,gap=0;std::uint64_t start=0;unsigned captureXruns=0,playbackXruns=0,transforms=0;};
}
int main(int argc,char** argv){try{
  require(argc==9,"usage: probe core|chain|combined|noop seconds none|count|sample 64|128 wisdom data-root output-prefix alsa|offline");
  const std::string mode=argv[1],profile=argv[3],prefix=argv[7],io=argv[8];
  const unsigned seconds=static_cast<unsigned>(std::stoul(argv[2])),block=static_cast<unsigned>(std::stoul(argv[4]));
  require(seconds>0 && seconds<=600 && (block==64 || block==128),"duration/block");
  require(mode=="core"||mode=="chain"||mode=="combined"||mode=="noop","mode");
  require(profile=="none"||profile=="count"||profile=="sample","profile");
  require(io=="alsa"||io=="offline","io");
  require(profile!="sample"||seconds<=20,"sample duration capacity");
  const bool hasCore=mode=="core"||mode=="combined",hasChain=mode=="chain"||mode=="combined";
  cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(2,&mask);require(sched_setaffinity(0,sizeof(mask),&mask)==0,"CPU2 affinity");
  require(fftwf_import_wisdom_from_filename(argv[5])!=0,"wisdom import");
  ardor::pog3::Pog3Processor core;
  std::string error;
  if(hasCore)require(core.configure({{"expression_mode",0},{"focus",1},{"dry_level",.25},{"down2_level",.25},
    {"down1_level",.25},{"fifth_level",.25},{"up1_level",.25},{"up2_level",.25},{"attack",.11},
    {"dry_attack",1},{"dry_filter",1},{"dry_detune",1},{"detune",1},{"spread",1},{"filter_q",1}},48000,error),error);
  ardor::PedalEngine chain;
  if(hasChain){ardor::PresetStore store(argv[6]);ardor::EngineLoadOptions options;options.blockSize=block;
    options.parallelRigs=true;options.rigWorkerCpu=3;options.wdwAudioCpu=2;options.wdwDryWorkerCpu=0;options.wdwWetWorkerCpu=1;
    require(ardor::applyPresetSlot(chain,store,{0,0},argv[6],options,error),error);}
  std::vector<std::array<float,2>> input(4*48000);
  std::uint32_t random=0x504f4733;
  for(std::size_t i=0;i<input.size();++i){
    random=1664525*random+1013904223;
    const float noise=(static_cast<double>(random)/4294967296.0-.5)*.03;
    const double t=static_cast<double>(i)/48000,twopi=6.283185307179586;
    const float chord=.06*(std::sin(twopi*82.4069*t)+std::sin(twopi*130.8128*t)+std::sin(twopi*164.8138*t)+std::sin(twopi*196*t));
    const double f=i<38400?82.4069:i<86400?110:i<105600?146.8324:196;
    input[i]={static_cast<float>(.04*(std::sin(twopi*f*t)+std::sin(twopi*f*1.5*t)+std::sin(twopi*f*2*t)))+.1f*(chord+noise),
      static_cast<float>(.03*(std::sin(twopi*f*1.25*t)+std::sin(twopi*f*2.5*t)))+.1f*(-.73f*chord+.4f*noise)};
  }
  const std::size_t warm=4*48000/block,total=seconds*48000/block;
  std::vector<Row> rows(warm+total);std::size_t completed=0;
  unsigned failedCapture=0,failedPlayback=0;bool profilingStarted=false;
  std::array<float,128> mono{},left{},right{};
  std::array<std::int32_t,256> captured{},silence{}; std::array<std::int32_t,768> prime{};
  Counters counters;Samples samples;
  if(profile=="count"){
    counters.add("cycles",PERF_TYPE_HARDWARE,PERF_COUNT_HW_CPU_CYCLES);
    counters.add("instructions",PERF_TYPE_HARDWARE,PERF_COUNT_HW_INSTRUCTIONS);
    counters.add("l1d_access",PERF_TYPE_RAW,0x04);counters.add("l1d_refill",PERF_TYPE_RAW,0x03);
    counters.add("branch_mispredict",PERF_TYPE_RAW,0x10);
  }
  if(profile=="sample")samples.prepare();
  {std::ifstream maps("/proc/self/maps");std::ofstream out(prefix+".maps");out<<maps.rdbuf();}
  Pcm capture,playback;
  if(io=="alsa"){
    capture.open(SND_PCM_STREAM_CAPTURE,block);playback.open(SND_PCM_STREAM_PLAYBACK,block);
    sched_param param{};param.sched_priority=70;require(sched_setscheduler(0,SCHED_FIFO,&param)==0,"FIFO70");
    playback.transfer(prime.data(),3*block,false);
  }
  const auto runStart=ns();std::uint64_t previousStart=0;double checksum=0;
  for(std::size_t b=0;b<warm+total;++b){
    unsigned captureXruns=io=="alsa"?capture.transfer(captured.data(),block,true):0;
    if(captureXruns){failedCapture=captureXruns;break;}
    if(b==warm){if(profile=="count")counters.begin();if(profile=="sample")samples.begin();profilingStarted=true;}
    const auto start=ns(),cpuStart=ns(CLOCK_THREAD_CPUTIME_ID);const auto transforms=core.transformCount();
    {
      ardor::ScopedDenormalGuard guard;
      for(unsigned i=0;i<block;++i){const auto& x=input[(b*block+i)%input.size()];
        if(hasCore){const auto y=core.process({x[0],x[1]}).mixed;left[i]=y.left;right[i]=y.right;mono[i]=.5f*(y.left+y.right);}
        else {mono[i]=x[0];left[i]=x[0];right[i]=x[1];}
      }
      if(hasChain)chain.processBlock(mono.data(),left.data(),right.data(),block);
      for(unsigned i=0;i<block;++i)checksum+=left[i]+right[i];
    }
    const auto cpuEnd=ns(CLOCK_THREAD_CPUTIME_ID),end=ns();
    unsigned playbackXruns=io=="alsa"?playback.transfer(silence.data(),block,false):0;
    rows[b]={(end-start)/1000.0,(cpuEnd-cpuStart)/1000.0,previousStart?(start-previousStart)/1000.0:0,
      start,captureXruns,playbackXruns,static_cast<unsigned>(core.transformCount()-transforms)};
    ++completed;
    previousStart=start;
    if(playbackXruns){failedPlayback=playbackXruns;break;}
  }
  if(io=="alsa"){sched_param p{};require(sched_setscheduler(0,SCHED_OTHER,&p)==0,"restore scheduler");}
  if(profile=="count" && profilingStarted)counters.end(prefix+".counters.csv");
  if(profile=="sample" && profilingStarted)samples.end(prefix+".samples.csv");
  require(std::isfinite(checksum) && (!hasCore || (core.healthy() && core.deadlineMisses()==0)),"DSP health/checksum");
  const bool failed=failedCapture||failedPlayback;
  // Preserve the lead-up to an xrun, including warmup, in the failure trace.
  // Successful runs report only the requested post-warmup measurement window.
  rows.resize(completed);
  if(!failed)rows.erase(rows.begin(),rows.begin()+warm);
  std::cerr<<"completed_callbacks="<<completed<<" warmup_callbacks="<<warm
    <<" first_capture_xrun="<<failedCapture<<" first_playback_xrun="<<failedPlayback<<'\n';
  std::ofstream raw(prefix+".callbacks.csv");raw<<"block,wall_us,thread_cpu_us,start_gap_us,start_ns,capture_xruns,playback_xruns,transforms\n";
  std::vector<double> sorted;sorted.reserve(rows.size());double sum=0,cpu=0;std::uint64_t over=0,cx=0,px=0,gaps=0;
  for(std::size_t i=0;i<rows.size();++i){const auto& r=rows[i];sorted.push_back(r.wall);sum+=r.wall;cpu+=r.cpu;
    over+=r.wall>block/.048;gaps+=r.gap>1.5*block/.048;cx+=r.captureXruns;px+=r.playbackXruns;
    raw<<i<<','<<r.wall<<','<<r.cpu<<','<<r.gap<<','<<r.start<<','<<r.captureXruns<<','<<r.playbackXruns<<','<<r.transforms<<'\n';}
  cx+=failedCapture;
  std::sort(sorted.begin(),sorted.end());require(bool(raw) && !sorted.empty(),"callback output/empty run");
  auto pct=[&](double p){return sorted[static_cast<std::size_t>((sorted.size()-1)*p)];};
  std::cout<<"mode,io,profile,frames,callbacks,budget_us,mean_us,thread_cpu_mean_us,p99_us,p999_us,max_us,over_period,start_gaps,capture_xruns,playback_xruns,elapsed_seconds,checksum,complete,warmup_included\n"
    <<std::setprecision(12)<<mode<<','<<io<<','<<profile<<','<<block<<','<<rows.size()<<','<<block/.048<<','<<sum/rows.size()<<','<<cpu/rows.size()<<','
    <<pct(.99)<<','<<pct(.999)<<','<<sorted.back()<<','<<over<<','<<gaps<<','<<cx<<','<<px<<','<<(ns()-runStart)/1e9<<','<<checksum<<','<<!failed<<','<<failed<<'\n';
  return failed?3:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
