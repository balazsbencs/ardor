#!/usr/bin/env python3
"""Generate single-owner diagnostic copies; production targets have no timers."""
from pathlib import Path
import sys


def replace_once(source, anchor, replacement):
    if source.count(anchor) != 1:
        raise RuntimeError(f"callback profile anchor changed: {anchor!r}")
    return source.replace(anchor, replacement)


def main():
    root, output = map(Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    (output / 'callback_profile.h').write_text(r'''#pragma once
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>
namespace pog3_phase {
inline constexpr std::array<const char*,8> labels{
  "forward_fft", "inverse_fft", "interpretation", "low_attack",
  "long_attack", "short_attack", "long_render", "short_render"};
struct Event {
  std::uint64_t sample=0, frame=0, inclusive=0, exclusive=0;
  std::uint32_t size=0, stage=0, control=0;
  int channel=-1, voice=-1;
};
struct Total { std::uint64_t calls=0, inclusive=0, exclusive=0; };
// This executable has one processing owner. Fixed diagnostic BSS is outside
// the processor preparation counter; no event storage is allocated on callback.
inline std::array<Event,65536> events{};
inline std::size_t count=0;
inline bool enabled=false, overflow=false, bankContext=false;
inline std::uint64_t sample=0;
inline int bankChannel=-1;
inline void beginSample(std::uint64_t value) {
  sample=value; bankContext=false; bankChannel=-1;
}
inline void reset() { enabled=false; overflow=false; count=0; beginSample(0); }
struct Scope {
  static inline Scope* current=nullptr;
  Scope* parent=nullptr;
  Event event{};
  std::uint64_t child=0;
  bool active=false;
  std::chrono::steady_clock::time_point start{};
  explicit Scope(unsigned stage, unsigned size=0, std::int64_t frame=-1,
                 int channel=-1, int voice=-1, std::uint32_t control=0):active(enabled) {
    if (!active) return;
    parent=current;
    if (parent) event=parent->event;
    else {
      event.sample=sample;
      event.frame=bankContext && bankChannel>=0 ? sample-bankChannel : sample;
      event.channel=bankContext ? bankChannel : -1;
    }
    event.stage=stage;
    if (size) event.size=size;
    if (frame>=0) event.frame=frame;
    if (channel!=-1) event.channel=channel;
    if (voice>=0) event.voice=voice;
    event.control=control;
    current=this;
    start=std::chrono::steady_clock::now();
  }
  ~Scope() {
    if (!active) return;
    event.inclusive=std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now()-start).count();
    event.exclusive=event.inclusive-child;
    current=parent;
    if (parent) parent->child+=event.inclusive;
    if (count<events.size()) events[count++]=event;
    else overflow=true;
  }
};
inline void report(const char* workload, unsigned frames, const std::vector<double>& times) {
  if (enabled || Scope::current || overflow) throw std::runtime_error("callback profile bounds/state failed");
  // Allocate, aggregate and format only after the callback allocation scope.
  std::vector<std::array<Total,8>> totals(times.size());
  std::ostringstream log;
  for (std::size_t i=0; i<count; ++i) {
    const auto& e=events[i];
    if (!e.sample || e.sample>times.size()*frames || e.stage>=labels.size()
        || e.exclusive>e.inclusive || e.frame>e.sample)
      throw std::runtime_error("callback profile event rejected");
    auto& t=totals[(e.sample-1)/frames][e.stage];
    ++t.calls; t.inclusive+=e.inclusive; t.exclusive+=e.exclusive;
    // Retain every real core event's entry/input timestamp and identity.
    if (std::string_view(workload)=="matched_freeze_off")
      log<<"phase_event,"<<workload<<','<<frames<<','<<labels[e.stage]<<','<<e.sample<<','
        <<e.frame<<','<<e.size<<','<<e.channel<<','<<e.voice<<','<<e.control<<','
        <<e.inclusive<<','<<e.exclusive<<'\n';
  }
  for (std::size_t block=0; block<times.size(); ++block) {
    const auto phase=((block+1)*frames)%512;
    std::uint64_t exclusive=0;
    for (unsigned stage=0; stage<labels.size(); ++stage) {
      const auto& t=totals[block][stage]; exclusive+=t.exclusive;
      if (t.calls) log<<"phase_stage,"<<workload<<','<<frames<<','<<block<<','<<phase<<','
        <<labels[stage]<<','<<t.calls<<','<<t.inclusive<<','<<t.exclusive<<'\n';
    }
    log<<"phase_callback,"<<workload<<','<<frames<<','<<block<<','<<phase<<','
      <<times[block]<<','<<(times[block]>frames/.048)<<','<<exclusive<<'\n';
  }
  log<<"phase_receipt,"<<workload<<','<<frames<<','<<count<<",overflow=0\n";
  std::cerr<<log.str();
}
}
''')
    directory = root / 'src/daisyfx/pog3'
    source = (directory / 'SpectralFrameStream.cpp').read_text()
    for anchor, scope in [
        ('void SpectralPlan::transform(std::vector<std::complex<float>>& values, bool inverse) const {',
         'pog3_phase::Scope phaseScope(inverse ? 1 : 0, static_cast<unsigned>(frameSize()));'),
        ('bool SpectralPlan::inverseForSynthesis(std::vector<std::complex<float>>& values) const noexcept {',
         'pog3_phase::Scope phaseScope(1, static_cast<unsigned>(frameSize()));')]:
        source = replace_once(source, anchor, anchor + '\n  ' + scope)
    (output / 'SpectralFrameStream.cpp').write_text('#include "callback_profile.h"\n' + source)

    source = (directory / 'PolyphonicPitchBank.cpp').read_text()
    anchor = '  ++inputSamples_;'
    source = replace_once(source, anchor, anchor + '\n  if (pog3_phase::enabled) { pog3_phase::sample=inputSamples_; pog3_phase::bankContext=true; }')
    anchor = '  for (std::size_t channel = 0; channel < 2; ++channel) {\n    for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {'
    source = replace_once(source, anchor, anchor.replace('\n    for', '\n    if (pog3_phase::enabled) pog3_phase::bankChannel=static_cast<int>(channel);\n    for', 1))
    for name, size in [('long', 2048), ('short', 1024), ('low', 4096)]:
        anchor = f'      {name}Frames_[channel].update({name}Analysis_[channel].spectrum());'
        source = replace_once(source, anchor,
            '      { pog3_phase::Scope phaseScope(2, ' + str(size) + ', inputSamples_ - channel, static_cast<int>(channel));\n' + anchor + '\n      }')
    for anchor, stage, size, stamp in [
        ('    lowGains_ = attack_.update(lowFrames_[0], lowFrames_[1], 2, lowInputEnd_);', 3, 4096, 'lowInputEnd_'),
        ('    longGains_ = attack_.update(longFrames_[0], longFrames_[1], 0, inputSamples_ - longAge_);', 4, 2048, 'inputSamples_ - longAge_'),
        ('    shortGains_ = attack_.update(shortFrames_[0], shortFrames_[1], 1, inputSamples_ - shortAge_);', 5, 1024, 'inputSamples_ - shortAge_')]:
        source = replace_once(source, anchor,
            f'    {{ pog3_phase::Scope phaseScope({stage}, {size}, {stamp}, -2, -1, std::bit_cast<std::uint32_t>(attack_.seconds()));\n' + anchor + '\n    }')
    for name, stage, size, voice in [('long', 6, 2048, 'voice'), ('short', 7, 1024, 'voice + 4')]:
        anchor = f'    const auto voice = {name}Job_ / 2, channel = {name}Job_ % 2;'
        source = replace_once(source, anchor, anchor +
            f'\n    pog3_phase::Scope phaseScope({stage}, {size}, inputSamples_ - {name}Age_, static_cast<int>(channel), static_cast<int>({voice}));')
    (output / 'PolyphonicPitchBank.cpp').write_text('#include "callback_profile.h"\n' + source)
    for name in ('PolyphonicAttack.cpp', 'SpectralFreeze.cpp'):
        (output / name).write_bytes((directory / name).read_bytes())

    source = (root / 'tests/pog3_bench.cpp').read_text()
    anchor = 'processor->reset();\n  std::vector<double> times'
    source = replace_once(source, anchor, 'processor->reset();\n  pog3_phase::reset();\n  std::vector<double> times')
    anchor = '    for (std::size_t block = 0; block < times.size(); ++block) {'
    source = replace_once(source, anchor, '    pog3_phase::enabled=true;\n' + anchor)
    anchor = '        const auto out = processor->process(input[i]);'
    source = replace_once(source, anchor, '        pog3_phase::beginSample(i + 1);\n' + anchor)
    anchor = '    // Reset clears health/deadline counters.'
    source = replace_once(source, anchor, '    pog3_phase::enabled=false;\n' + anchor)
    anchor = '  std::sort(times.begin(), times.end());'
    source = replace_once(source, anchor, '  pog3_phase::report(name, static_cast<unsigned>(callback), times);\n' + anchor)
    # Validate output with recording active, rather than merely testing the
    # diagnostic library's inactive numerical path.
    anchor = '      processor.reset();\n      for (const auto sample : freezeInput) {'
    source = replace_once(source, anchor, '      processor.reset();\n      pog3_phase::reset();\n      pog3_phase::enabled=true;\n      for (const auto sample : freezeInput) {')
    anchor = '      if (!output || !processor.healthy()) throw std::runtime_error("matched trace failed");'
    source = replace_once(source, anchor, '''      pog3_phase::enabled=false;
      if (pog3_phase::overflow || pog3_phase::Scope::current)
        throw std::runtime_error("recorded trace profile bounds/state failed");
      std::cerr << "phase_trace_events=" << pog3_phase::count << '\\n';
''' + anchor)
    (output / 'bench.cpp').write_text('#include "callback_profile.h"\n' + source)


if __name__ == '__main__':
    main()
