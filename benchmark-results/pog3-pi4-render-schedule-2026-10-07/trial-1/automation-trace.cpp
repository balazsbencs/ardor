#include "daisyfx/pog3/Pog3Processor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <fftw3.h>

namespace {
using namespace ardor::pog3;
constexpr std::size_t length=40960, drain=4096;
constexpr std::array<std::size_t,41> edges{
  0,1,2,7,8,9,16,17,18,37,38,39,47,48,49,63,64,65,79,80,81,
  95,96,97,101,102,103,110,111,112,126,127,128,129,130,190,191,192,193,240,241};
bool controlEvent(std::size_t i) {
  return std::find(edges.begin(),edges.end(),i%256)!=edges.end()
    || i%48==47 || i%48==0 || i%48==1 || i%512>=510;
}
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void emit(float value) {
  require(std::isfinite(value),"nonfinite trace");
  std::cout.write(reinterpret_cast<const char*>(&value),sizeof value);
}
PitchStereo inputAt(std::size_t i,std::uint32_t& random) {
  random=1664525*random+1013904223;
  if(i>=length-drain) return {};
  const auto t=static_cast<double>(i)/kSampleRate;
  const auto f=std::array<double,4>{82.4069,110,146.8324,196}[(i/4096)%4];
  const auto noise=static_cast<float>((static_cast<double>(random)/4294967296.0-.5)*.012);
  const auto amplitude=i%4096<47?.0:.06;
  return {static_cast<float>(amplitude*(std::sin(6.283185307179586*f*t)+.7*std::sin(6.283185307179586*f*1.5*t)))+noise,
          static_cast<float>(amplitude*(std::sin(6.283185307179586*f*1.25*t)-.4*std::sin(6.283185307179586*f*2.5*t)))-.3f*noise};
}
}
int main(int argc,char** argv) {
  try {
    require(argc==2,"usage: automation-trace wisdom");
    require(fftwf_import_wisdom_from_filename(argv[1])!=0,"wisdom import");
    PolyphonicPitchBank bank; bank.prepare();
    Pog3Processor processor; std::string error;
    require(processor.configure(nlohmann::json{{"dry_level",.6},{"down2_level",.3},{"down1_level",.4},
      {"up1_level",.5},{"up2_level",.5},{"fifth_level",.3},{"input_gain",.5},{"master_level",.5}},48000,error),"processor configure");
    std::size_t events=0;
    for(unsigned pass=0;pass<2;++pass) {
      bank.reset(); processor.reset();
      std::uint32_t random=0x504f4733;
      for(std::size_t i=0;i<length;++i) {
        if(i<length-drain && controlEvent(i)) {
          ++events;
          const float position=std::array<float,8>{0,.01f,.04f,.4f,.97f,.99f,1,.7f}[(i/97+pass)%8];
          const auto mode=static_cast<ExpressionMode>((i/2048+pass)%(kExperimentalFreezeEnabled?7:5));
          const float attack=std::array<float,4>{0,.002f,.11f,.4f}[(i/17+pass)%4];
          bank.setFocus(((i/193+pass)%2)!=0);
          require(bank.setAttackSeconds(attack),"bank Attack");
          require(bank.setWarp(position),"bank Warp");
          require(bank.setFreeze(mode,position,((i/241+pass)%2)!=0),"bank freeze");
          require(processor.setParameterTarget("attack",position),"processor Attack");
          require(processor.setParameterTarget("focus",((i/193+pass)%2)?1:0),"processor Focus");
          require(processor.setParameterTarget("dry_attack",((i/257+pass)%2)?1:0),"processor dry Attack");
          require(processor.setParameterTarget("expression_mode",static_cast<float>(mode)/6),"processor mode");
          require(processor.setParameterTarget("expression_position",position),"processor position");
        }
        const auto input=inputAt(i,random);
        for(const auto voice:bank.process(input)) { emit(voice.left); emit(voice.right); }
        const auto output=processor.process(input);
        for(const auto value:{output.mixed.left,output.mixed.right,output.wet.left,output.wet.right,output.dry.left,output.dry.right,
          processor.generatedGainTarget(),processor.warpTarget(),processor.filterCutoff(),processor.filterEnvelope(),
          static_cast<float>(processor.freezeCaptures()),static_cast<float>(processor.freezeTargets())}) emit(value);
        require(bank.healthy()&&bank.deadlineMisses()==0&&processor.healthy()&&processor.deadlineMisses()==0,"render deadline/health");
      }
    }
    require(static_cast<bool>(std::cout),"trace write");
    std::cerr<<"automation_trace,passes=2,samples="<<2*length<<",floats="<<2*length*24
      <<",control_events="<<events<<",drain_per_pass="<<drain<<",experimental_freeze="<<kExperimentalFreezeEnabled<<'\n';
    return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
