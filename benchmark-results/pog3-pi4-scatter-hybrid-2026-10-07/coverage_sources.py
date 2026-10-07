#!/usr/bin/env python3
"""Generate diagnostic source copies; model four-source versus hybrid traversal."""
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)

def replace_once(source, anchor, replacement):
    assert source.count(anchor) == 1, anchor
    return source.replace(anchor, replacement)

(output / 'coverage.h').write_text(r'''#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
namespace scatter_coverage {
struct Counts {
  std::uint64_t regions=0, integerSources=0, fourBatches=0, pairBatches=0,
                singleInteriorSources=0, edgeSources=0;
};
inline std::array<Counts,2> totals{};
inline void inspect(int first, int last, int shift, float position, int half, int radius) {
  assert(radius==12);
  for (unsigned variant=0; variant<2; ++variant) {
    auto& count=totals[variant]; ++count.regions;
    if (position==0) { count.integerSources+=last-first+1; continue; }
    for (int k=first; k<=last; ++k) {
      if (k+3<=last && k+shift>radius && k+3+shift+radius-1<half) {
        ++count.fourBatches; k+=3;
      } else if (variant && k+1<=last && k+shift>radius && k+1+shift+radius-1<half) {
        ++count.pairBatches; ++k;
      } else if (k+shift>radius && k+shift+radius-1<half) ++count.singleInteriorSources;
      else ++count.edgeSources;
    }
  }
}
inline void report(const char* workload, unsigned frames) {
  for (unsigned variant=0; variant<2; ++variant) {
    const auto& count=totals[variant];
    std::cerr<<"scatter_geometry,"<<workload<<','<<frames<<','<<(variant?"hybrid":"four")<<','
      <<count.regions<<','<<count.integerSources<<','<<count.fourBatches<<','<<count.pairBatches<<','
      <<count.singleInteriorSources<<','<<count.edgeSources<<','
      <<count.fourBatches*27+count.pairBatches*25+count.singleInteriorSources*24<<','
      <<(count.fourBatches*4+count.pairBatches*2+count.singleInteriorSources)*24<<'\n';
  }
}
}
''')
source = (root / 'src/daisyfx/pog3/PolyphonicPitchBank.cpp').read_text()
anchor = '    for (int k = static_cast<int>(region.first); k <= static_cast<int>(region.last); ++k) {'
source = replace_once(source, anchor, '''    scatter_coverage::inspect(region.first, region.last, shiftCeil, position, half, PitchPlan::kRadius);
''' + anchor)
(output / 'pitch.cpp').write_text('#include "coverage.h"\n' + source)
source = (root / 'tests/pog3_bench.cpp').read_text()
anchor = 'processor->reset();\n  std::vector<double> times'
source = replace_once(source, anchor, 'processor->reset();\n  scatter_coverage::totals = {};\n  std::vector<double> times')
anchor = '  std::sort(times.begin(), times.end());'
source = replace_once(source, anchor, '  scatter_coverage::report(name, callback);\n' + anchor)
(output / 'bench.cpp').write_text('#include "coverage.h"\n' + source)
