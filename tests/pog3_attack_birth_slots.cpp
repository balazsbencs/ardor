#include "daisyfx/pog3/AttackBirthSlots.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace {
constexpr std::size_t capacity=256;
struct Partial { std::uint64_t generation=0; std::int64_t seen=0; };
using Partials=std::array<Partial,capacity>;
using Flags=std::array<bool,capacity>;
// Immutable original selection oracle; never shares the candidate's ordering.
std::size_t original(const Partials& partials,const Flags& used,const Flags& reserved) {
  std::size_t slot=partials.size();
  std::int64_t oldest=std::numeric_limits<std::int64_t>::max();
  for(std::size_t s=0;s<partials.size();++s)
    if(!used[s]&&!reserved[s]&&(!partials[s].generation||partials[s].seen<oldest)) {
      slot=s;oldest=partials[s].seen;
      if(!partials[s].generation)break;
    }
  return slot;
}
}
int main() {
  try {
    std::mt19937 random(0x504f4733);
    ardor::pog3::detail::AttackBirthSlots<capacity> slots;
    std::size_t selections=0;
    constexpr std::array<std::int64_t,7> times{0,1,42,-1,std::numeric_limits<std::int64_t>::min(),
      std::numeric_limits<std::int64_t>::max(),std::numeric_limits<std::int64_t>::max()-1};
    for(unsigned scenario=0;scenario<2048;++scenario) {
      Partials partials{};Flags used{},reserved{};
      for(std::size_t i=0;i<capacity;++i) {
        partials[i]={random()%5?1U:0U,times[random()%times.size()]};
        used[i]=random()%7==0;reserved[i]=random()%5==0;
        if(scenario==0) {partials[i]={0,times[i%times.size()]};used[i]=reserved[i]=false;}
        if(scenario==1) {partials[i]={1,42};used[i]=reserved[i]=false;}
        if(scenario==2) {partials[i]={1,std::numeric_limits<std::int64_t>::max()};used[i]=reserved[i]=false;}
        if(scenario==3) reserved[i]=true;
        if(scenario==4) used[i]=true;
      }
      slots.reset();
      for(std::size_t i=0;i<capacity;++i)if(!used[i]&&!reserved[i])slots.add(i,partials[i].generation,partials[i].seen);
      for(std::size_t step=0;step<capacity+2;++step) {
        // An intervening surviving binding may use a slot; never make a used
        // slot available again within one update. Mutate only that used state.
        if(scenario>=5&&step%3==0) {const auto s=random()%capacity;used[s]=true;partials[s]={1,12345};}
        const auto expected=original(partials,used,reserved),actual=slots.take(used);++selections;
        if(expected!=actual)throw std::runtime_error("birth allocation priority changed");
        if(actual<capacity){used[actual]=true;partials[actual]={1,100000};}
      }
    }
    std::cout<<"Attack birth-slot order matches original for "<<selections<<" selections, including empty/oldest ties, reservations, intervening uses, exhaustion, reset and INT64 boundaries.\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
