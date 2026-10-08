#include <bit>
#include <complex>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
float norm(std::complex<float> z) { const float x=z.real(),y=z.imag(); const float square=x*x+y*y; if(square>=std::numeric_limits<float>::min() && std::isfinite(square)) return std::sqrt(square); const double r=x,i=y; return static_cast<float>(std::sqrt(r*r+i*i)); }
int main() {
 std::mt19937 rng(0x504f4733); unsigned worst=0; std::uint64_t mismatches=0, checked=0;
 for (unsigned j=0;j<8000000;++j) {
  const float r=std::bit_cast<float>(static_cast<std::uint32_t>(rng())),i=std::bit_cast<float>(static_cast<std::uint32_t>(rng()));
  if(!std::isfinite(r)||!std::isfinite(i))continue;
  const float a=std::abs(std::complex<float>{r,i}),b=norm({r,i});
  if(std::isfinite(a)!=std::isfinite(b)){std::cerr<<"finite classification changed\n";return 1;}
  if(!std::isfinite(a))continue;
  const auto x=std::bit_cast<std::uint32_t>(a),y=std::bit_cast<std::uint32_t>(b);
  const auto d=x>y?x-y:y-x;worst=std::max(worst,d); mismatches+=d!=0;++checked;
 }
 std::cout<<"finite pairs="<<checked<<" changed="<<mismatches<<" worst ulps="<<worst<<'\n';
 return worst>1;
}
