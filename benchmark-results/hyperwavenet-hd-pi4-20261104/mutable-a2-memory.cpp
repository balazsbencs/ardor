#include "namparam/wavenet/mutable_a2.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
int main() {
  std::vector<float> weights(47202); unsigned seed=731;
  for(auto& w:weights) { seed=seed*1664525u+1013904223u; w=(int(seed>>16)-32768)*0.0000005f; }
  weights.back()=0.01f;
  nam::wavenet::MutableA2 dsp(weights,48000);dsp.SetPrewarmOnReset(false);
  for(int capacity:{1,3,4,13,64,128,129,257,1024}) {
    dsp.Reset(48000,capacity);
    std::vector<float> x(capacity),y(capacity+2,12345.f);auto* in=x.data();auto* out=y.data()+1;
    for(int n=0;n<100;++n) {
      const int frames=1+(n*71)%capacity;
      for(int f=0;f<frames;++f)x[f]=0.05f*std::sin((n*capacity+f)*0.071f);
      dsp.process(&in,&out,frames);
      if(y.front()!=12345.f||y.back()!=12345.f)return 1;
      for(int f=0;f<frames;++f)if(!std::isfinite(out[f]))return 2;
      if(n%7==0)dsp.setWeights(weights);
    }
  }
  std::cout<<"ASan/UBSan variable-block history/reset/weight-update checks passed\n";
}
