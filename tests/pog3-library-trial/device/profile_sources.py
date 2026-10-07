#!/usr/bin/env python3
"""Generate standalone diagnostic copies; never instrument production targets."""
from pathlib import Path
import sys


def replace_once(source, anchor, replacement):
    if source.count(anchor) != 1:
        raise RuntimeError(f"profile anchor changed: {anchor!r}")
    return source.replace(anchor, replacement)


def main():
    root, output = map(Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    (output / "profile_timer.h").write_text('''#pragma once
#include <array>
#include <chrono>
#include <cstdint>
namespace pog3_profile {
struct Total { std::uint64_t calls=0, inclusive=0, exclusive=0; };
inline thread_local std::array<Total,6> totals{};
inline thread_local std::array<std::uint64_t,4> transforms{};
struct Scope {
  static inline thread_local Scope* current=nullptr;
  Scope* parent; std::uint64_t child=0; unsigned id;
  std::chrono::steady_clock::time_point start;
  explicit Scope(unsigned i):parent(current),id(i),start(std::chrono::steady_clock::now()){current=this;}
  ~Scope(){auto t=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    ++totals[id].calls;totals[id].inclusive+=t;totals[id].exclusive+=t-child;
    current=parent;if(parent)parent->child+=t;}
};
}
''')
    anchors = {
        "SpectralFrameStream.cpp": [("void SpectralPlan::transform(std::vector<std::complex<float>>& values, bool inverse) const {", 0)],
        "PolyphonicPitchBank.cpp": [
            ("void PitchFrame::update(std::span<const std::complex<float>> spectrum) noexcept {", 2),
            ("float heldMix, float heldGain, const PitchRenderer* heldReference) noexcept {", 1),
            ("const PitchRenderer* reference) noexcept {", 5)],
        "PolyphonicAttack.cpp": [("std::size_t resolution, std::int64_t inputEnd) noexcept {", 3)],
        "SpectralFreeze.cpp": [("std::int64_t inputEnd) noexcept {", 4)],
    }
    for name, scopes in anchors.items():
        source = (root / "src/daisyfx/pog3" / name).read_text()
        if name == "SpectralFrameStream.cpp":
            anchor = "bool SpectralPlan::inverseForSynthesis(std::vector<std::complex<float>>& values) const noexcept {"
            if anchor in source:
                source = replace_once(source, anchor, anchor + "\n  pog3_profile::Scope profileScope(0);")
            executions = [
                ("fftwf_execute_dft(plan, data, data);", 0),
                ("fftwf_execute_dft_r2c(aligned ? realForward : unalignedRealForward, packed, data);", 1),
                ("fftwf_execute_dft_c2r(aligned ? realInverse : unalignedRealInverse, data, packed);", 2),
            ]
            for anchor, category in executions:
                if anchor in source:
                    source = replace_once(source, anchor,
                        f"++pog3_profile::transforms[{category}];\n"
                        "    if (!aligned) ++pog3_profile::transforms[3];\n    " + anchor)
        for anchor, category in scopes:
            source = replace_once(source, anchor, anchor + f"\n  pog3_profile::Scope profileScope({category});")
        (output / name).write_text('#include "profile_timer.h"\n' + source)
    source = (root / "tests/pog3_bench.cpp").read_text()
    anchor = "processor->reset();\n  std::vector<double> times"
    source = replace_once(source, anchor, "processor->reset();\n  pog3_profile::totals = {};\n  pog3_profile::transforms = {};\n  std::vector<double> times")
    anchor = "  std::sort(times.begin(), times.end());"
    report = '''  const std::array<const char*,6> labels{"FFT","render_excluding_fft_and_held","frame_interpretation","Attack","freeze_update","held_render"};
  const double elapsed = meanUs * times.size();
  for (std::size_t i=0; i<labels.size(); ++i) {
    const auto& x = pog3_profile::totals[i];
    std::cerr << name << ',' << callback << ',' << labels[i] << ',' << x.calls << ','
      << x.inclusive/1000.0 << ',' << x.exclusive/1000.0 << ','
      << 100.0*x.exclusive/1000.0/elapsed << '\\n';
  }
  std::cerr << "FFT_dispatch," << name << ',' << callback;
  for (const auto count : pog3_profile::transforms) std::cerr << ',' << count;
  std::cerr << '\\n';
'''
    source = replace_once(source, anchor, report + anchor)
    (output / "bench.cpp").write_text('#include "profile_timer.h"\n' + source)


if __name__ == "__main__":
    main()
