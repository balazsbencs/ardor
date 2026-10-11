// Target benchmark: full serial PedalEngine, including every NAM boundary,
// mono/stereo copies, gain smoothing, sanitization and final safety limiter.
#include "dsp/PedalEngine.h"
#include "dsp/DenormalGuard.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <vector>
#if defined(__linux__)
#include <sched.h>
#include <sys/mman.h>
#endif

namespace {
std::vector<int> list(const std::string& text, int maximum) {
  std::vector<int> result;
  std::istringstream stream(text);
  std::string token;
  while (std::getline(stream, token, ',')) {
    size_t consumed = 0;
    int value = std::stoi(token, &consumed);
    if (consumed != token.size() || value < 1 || value > maximum)
      throw std::runtime_error("invalid list: " + text);
    result.push_back(value);
  }
  if (result.empty()) throw std::runtime_error("empty list");
  return result;
}
double percentile(const std::vector<double>& sorted, double fraction) {
  return sorted.at(static_cast<size_t>(std::ceil(fraction * (sorted.size()-1))));
}
long residentKiB() {
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status,line)) if (line.starts_with("VmRSS:")) {
    std::istringstream input(line.substr(6)); long value=0; input >> value; return value;
  }
  return -1;
}
std::string csv(const std::string& value) {
  std::string result="\"";
  for (char c:value) { if(c=='"') result+='"'; result+=c; }
  return result+'"';
}
}

int main(int argc, char** argv) try {
  std::vector<int> framesList{32,64,128}, counts{1,2,3,4,6,8,12,16};
  int iterations=10000, warmup=500, cpu=2;
  float tier=1;
  bool cab=false;
  std::filesystem::path model, amp;
  float ampTier=1;
  nlohmann::json parameters=nlohmann::json::object();
  for(int i=1;i<argc;++i) {
    std::string argument=argv[i];
    auto next=[&]() -> std::string {
      if(++i>=argc) throw std::runtime_error("missing option value");
      return argv[i];
    };
    if(argument=="--block-sizes") framesList=list(next(),4096);
    else if(argument=="--counts") counts=list(next(),64);
    else if(argument=="--iterations") iterations=list(next(),10000000).at(0);
    else if(argument=="--warmup") warmup=list(next(),1000000).at(0);
    else if(argument=="--cpu") { cpu=std::stoi(next()); if(cpu<0 || cpu>=1024) throw std::runtime_error("invalid CPU"); }
    else if(argument=="--tier") { tier=std::stof(next()); if(!std::isfinite(tier) || tier<0 || tier>1) throw std::runtime_error("invalid tier"); }
    else if(argument=="--parameters") { parameters=nlohmann::json::parse(next()); if(!parameters.is_object()) throw std::runtime_error("parameters must be an object"); }
    else if(argument=="--cab") cab=true;
    else if(argument=="--amp") amp=next();
    else if(argument=="--amp-tier") { ampTier=std::stof(next()); if(!std::isfinite(ampTier) || ampTier<0 || ampTier>1) throw std::runtime_error("invalid amp tier"); }
    else if(argument=="--help") {
      std::cout << "Usage: pedal-nam-chain-bench [--block-sizes 32,64,128] [--counts 1,2,3,4,6,8,12,16] [--iterations 10000] [--warmup 500] [--cpu 2] [--tier 0|1] [--parameters JSON] [--amp MODEL] [--amp-tier 0|1] [--cab] MODEL\n";
      return 0;
    } else if(argument.starts_with('-') || !model.empty()) throw std::runtime_error("unknown argument: "+argument);
    else model=argument;
  }
  if(model.empty()) throw std::runtime_error("model required");
#if defined(__linux__)
  cpu_set_t affinity; CPU_ZERO(&affinity); CPU_SET(cpu,&affinity);
  if(sched_setaffinity(0,sizeof(affinity),&affinity)) throw std::runtime_error("CPU affinity failed");
  bool locked=mlockall(MCL_CURRENT|MCL_FUTURE)==0;
  std::cout << "# mlockall=" << (locked ? "ok":"failed") << '\n';
#endif
  const ardor::ScopedDenormalGuard guard;
  std::cout << "# benchmark=ardor-nam-chain-v1\n# sample_rate=48000\n# cpu=" << cpu
            << "\n# input=alternating silence, harmonic/noise at 0.02 and 0.2 peak\n# parameters=" << parameters.dump()
            << "\n# build_type=" << ARDOR_BUILD_TYPE << "\n# compiler=" << ARDOR_COMPILER
            << "\n# compile_flags=" << ARDOR_CXX_FLAGS << '\n';
  std::cout << "model,amp_model,amp_tier,tier,nam_count,cab_frames,block_frames,iterations,load_ms,rss_kib,mean_us,p50_us,p99_us,p999_us,max_us,deadline_misses,mean_budget_pct,p999_budget_pct,max_budget_pct,nonfinite,peak,checksum\n";
  std::cout << std::fixed << std::setprecision(6);
  for(int frames:framesList) for(int count:counts) {
    ardor::PedalEngine engine;
    auto loadStart=std::chrono::steady_clock::now();
    engine.setSampleRate(48000); engine.prepareBlockSize(frames);
    for(int index=0;index<count;++index)
      // codeql[cpp/path-injection]: this standalone benchmark intentionally reads
      // the operator's CLI-selected model; it has no remote input or service role.
      if(!engine.loadNam(model,48000,frames,"nam-"+std::to_string(index),tier,
                         ardor::NamInputMode::Sum,std::nullopt,parameters))
        throw std::runtime_error("NAM load failed: "+model.string());
    // codeql[cpp/path-injection]: the optional amp is another intentional CLI
    // input to this standalone benchmark, running with the operator's privileges.
    if(!amp.empty() && !engine.loadNam(amp,48000,frames,"amp",ampTier))
      throw std::runtime_error("amp load failed: "+amp.string());
    if(cab) {
      std::vector<float> impulse(8192);
      impulse[0]=.5f;
      for(size_t i=1;i<impulse.size();++i)
        impulse[i]=.006f*std::exp(-static_cast<float>(i)/1500)*std::sin(i*.19f);
      engine.addCab(std::move(impulse),1,1,"cab");
    }
    const double loadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-loadStart).count();
    std::vector<float> input(frames),left(frames),right(frames);
    uint32_t random=1457;
    auto generate=[&](int block) {
      const float amplitude=block%300<100 ? 0 : block%300<200 ? .02f:.2f;
      for(int i=0;i<frames;++i) {
        random=random*1664525u+1013904223u;
        const double time=static_cast<double>(block)*frames+i;
        input[i]=amplitude*(.5f*std::sin(time*.037)+.3f*std::sin(time*.173)+.2f*(static_cast<float>(random>>8)/16777216-.5f));
      }
    };
    for(int i=0;i<warmup;++i) { generate(i); engine.processBlock(input.data(),left.data(),right.data(),frames); }
    std::vector<double> timings; timings.reserve(iterations);
    int misses=0; size_t nonfinite=0; float peak=0; double checksum=0;
    const double budget=static_cast<double>(frames)/48000*1e6;
    for(int block=0;block<iterations;++block) {
      generate(block+warmup);
      const auto start=std::chrono::steady_clock::now();
      engine.processBlock(input.data(),left.data(),right.data(),frames);
      const double elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
      timings.push_back(elapsed); if(elapsed>budget) ++misses;
      for(int i=0;i<frames;++i) {
        if(!std::isfinite(left[i]) || !std::isfinite(right[i])) ++nonfinite;
        peak=std::max(peak,std::max(std::fabs(left[i]),std::fabs(right[i])));
      }
      checksum+=left[block%frames];
    }
    nonfinite+=engine.nonFiniteBlockCount();
    std::sort(timings.begin(),timings.end());
    const double mean=std::accumulate(timings.begin(),timings.end(),0.0)/iterations;
    std::cout << csv(model.filename().string()) << ',' << csv(amp.filename().string()) << ',' << ampTier << ',' << tier << ',' << count+(!amp.empty()?1:0) << ',' << (cab?8192:0) << ',' << frames << ',' << iterations << ','
      << loadMs << ',' << residentKiB() << ',' << mean << ',' << percentile(timings,.5) << ',' << percentile(timings,.99) << ','
      << percentile(timings,.999) << ',' << timings.back() << ',' << misses << ',' << mean*100/budget << ','
      << percentile(timings,.999)*100/budget << ',' << timings.back()*100/budget << ',' << nonfinite << ',' << peak << ',' << checksum << '\n';
    std::cout.flush();
    if(nonfinite) return 1;
  }
  return 0;
} catch(const std::exception& error) {
  std::cerr << "nam-chain-bench: " << error.what() << '\n'; return 1;
}
