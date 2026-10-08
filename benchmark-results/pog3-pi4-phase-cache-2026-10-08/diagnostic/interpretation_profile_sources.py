#!/usr/bin/env python3
"""Standalone interpretation attribution; production code and arithmetic unchanged."""
from pathlib import Path
import sys
import callback_profile_sources as base


def main():
    root, output = map(Path, sys.argv[1:])
    base.main()
    header = '#include <complex>\n' + (output / 'callback_profile.h').read_text()
    header = base.replace_once(header, 'std::array<const char*,8> labels', 'std::array<const char*,14> labels')
    header = base.replace_once(header, '"long_attack", "short_attack", "long_render", "short_render"};',
        '"long_attack", "short_attack", "long_render", "short_render",\n'
        '  "frame_magnitudes", "frame_peak_frequency", "frame_candidate_sort",\n'
        '  "frame_predictions", "frame_association", "frame_finalize"};')
    header = base.replace_once(header, 'std::array<Total,8>', 'std::array<Total,14>')
    header = base.replace_once(header, 'std::array<Event,65536>', 'std::array<Event,131072>')
    records = r'''
struct FrameRecord {
  std::uint64_t sample=0, frame=0;
  std::uint32_t size=0, channel=0, bins=0, peaks=0, phase=0, logarithmic=0,
    phaseReuse=0, predicted=0, matched=0, births=0, firstSteps=0, fallbackSteps=0, selected=0, snapshots=0, snapshotReads=0, currentArgs=0, previousArgs=0;
};
inline std::array<FrameRecord,8192> frames{};
inline std::size_t frameCount=0;
inline FrameRecord* currentFrame=nullptr;
// Diagnostic shadow validity only, not cached DSP phase values. Max supported N=32768.
inline std::array<std::array<bool,16385>,6> previousPhase{};
'''
    header = base.replace_once(header, 'inline void reset() { enabled=false; overflow=false; count=0; beginSample(0); }',
        records + '''inline void reset() {
  enabled=false; overflow=false; count=frameCount=0; currentFrame=nullptr; beginSample(0);
  events.fill(Event{}); frames.fill(FrameRecord{});
  for (auto& flags:previousPhase) flags.fill(false);
}''')
    helper = r'''
inline float phaseArg(std::complex<float> value,bool previous) noexcept {
  if (enabled && currentFrame) {
    if (previous) ++currentFrame->previousArgs;
    else ++currentFrame->currentArgs;
  }
  return std::arg(value);
}
struct FrameScope {
  FrameRecord* record=nullptr;
  std::array<bool,16385>* validity=nullptr;
  explicit FrameScope(unsigned size) {
    if (!enabled) return;
    const auto* parent=Scope::current;
    if (!parent || parent->event.stage!=2 || parent->event.channel<0 || parent->event.channel>1
        || currentFrame || frameCount==frames.size() || (size!=1024 && size!=2048 && size!=4096)) {
      overflow=true; return;
    }
    record=&frames[frameCount++]; currentFrame=record;
    record->sample=sample; record->frame=parent->event.frame;
    record->size=size; record->channel=static_cast<unsigned>(parent->event.channel);
    const auto resolution=size==2048?0U:size==1024?1U:2U;
    validity=&previousPhase[resolution*2+record->channel];
  }
  bool visit(std::size_t bin) {
    if (!record) return false;
    if (bin>=validity->size()) { overflow=true; return false; }
    const bool old=(*validity)[bin]; (*validity)[bin]=false; return old;
  }
  void phase(std::size_t bin,bool reusable) {
    if (!record) return;
    ++record->phase; record->phaseReuse+=reusable;
    if (bin>=validity->size()) { overflow=true; return; }
    (*validity)[bin]=true;
  }
  ~FrameScope() { if (record) currentFrame=nullptr; }
};
inline void reportFrame(const char* workload,unsigned callback) {
  if (enabled || currentFrame || overflow) throw std::runtime_error("interpretation profile bounds/state failed");
  std::ostringstream log;
  if (std::string_view(workload)=="matched_freeze_off") for(std::size_t i=0;i<frameCount;++i) {
    const auto& r=frames[i];
    log<<"frame_counts,"<<workload<<','<<callback<<','<<r.sample<<','<<r.frame<<','<<r.size<<','<<r.channel<<','
      <<r.bins<<','<<r.peaks<<','<<r.phase<<','<<r.logarithmic<<','<<r.phaseReuse<<','<<r.predicted<<','
      <<r.matched<<','<<r.births<<','<<r.firstSteps<<','<<r.fallbackSteps<<','<<r.selected<<','<<r.snapshots<<','<<r.snapshotReads<<','<<r.currentArgs<<','<<r.previousArgs<<'\n';
  }
  log<<"frame_receipt,"<<workload<<','<<callback<<','<<frameCount<<",overflow=0\n";
  std::cerr<<log.str();
}
'''
    header = base.replace_once(header, 'inline void report(const char* workload,', helper+'\ninline void report(const char* workload,')
    (output / 'callback_profile.h').write_text(header)
    source = (output / 'PolyphonicPitchBank.cpp').read_text()
    def scope(stage):
        return f'pog3_phase::Scope frameChildScope({stage});'
    source = base.replace_once(source, '  const auto half = spectrum.size() / 2;',
        '  pog3_phase::FrameScope frameRecord(static_cast<unsigned>(spectrum.size()));\n  const auto half = spectrum.size() / 2;')
    source = base.replace_once(source, '  float maximum = 0;\n  for (std::size_t k = 0;',
        '  float maximum = 0;\n  { '+scope(8)+'\n  for (std::size_t k = 0;')
    source = base.replace_once(source, '  std::size_t candidates = 0;',
        '  }\n  if (frameRecord.record) frameRecord.record->bins=static_cast<unsigned>(magnitude_.size());\n  std::size_t candidates = 0;\n  { '+scope(9))
    source = base.replace_once(source, '  for (std::size_t k = 0; k <= peakLimit_; ++k) {',
        '  for (std::size_t k = 0; k <= peakLimit_; ++k) {\n    const bool phaseReusable=frameRecord.visit(k);')
    anchor='    else if (previous_ && magnitude(previousSpectrum_[k]) > 1e-7f) {'
    source=base.replace_once(source,anchor,anchor+'\n      frameRecord.phase(k,phaseReusable);')
    source=base.replace_once(source, '      const double a = std::log(std::max(magnitude_[k - 1], 1e-20f));',
        '      if (frameRecord.record) ++frameRecord.record->logarithmic;\n      const double a = std::log(std::max(magnitude_[k - 1], 1e-20f));')
    source=base.replace_once(source, '  if (candidates > regions_.size()) {',
        '  }\n  if (frameRecord.record) frameRecord.record->peaks=static_cast<unsigned>(candidates);\n  { '+scope(10)+'\n  if (candidates > regions_.size()) {')
    source=base.replace_once(source, '  std::array<bool, kMaxPitchPartials> used{};',
        '  }\n  std::array<bool, kMaxPitchPartials> used{};')
    aging='  for (auto& track : tracks_) if (track.generation) track.missed = std::min(track.missed + 1, 5U);'
    source=base.replace_once(source,aging,'')
    source=base.replace_once(source, '  std::size_t predicted = 0;',
        '  std::size_t predicted = 0;\n  { '+scope(11)+'\n'+aging)
    association='  bool birthSlotsReady = false;' if '  bool birthSlotsReady = false;' in source else '  for (std::size_t p = 0; p < candidates; ++p) {'
    source=base.replace_once(source, association,
        '  }\n  if (frameRecord.record) frameRecord.record->predicted=static_cast<unsigned>(predicted);\n  { '+scope(12)+'\n'+association)
    source=base.replace_once(source,'    bool birth = match == tracks_.size();',
        '    bool birth = match == tracks_.size();\n    if (frameRecord.record) { frameRecord.record->births+=birth; frameRecord.record->matched+=!birth; }')
    if 'birthSlots_.prepare(tracks_);' in source:
        source=base.replace_once(source, '        birthSlots_.prepare(tracks_);',
            '        if (frameRecord.record) { ++frameRecord.record->snapshots; frameRecord.record->snapshotReads+=2*tracks_.size(); }\n        birthSlots_.prepare(tracks_);')
    else:
        anchor='      for (std::size_t t = 0; t < tracks_.size(); ++t)\n        if (!used[t] && (!tracks_[t].generation || tracks_[t].missed > 4)) { match = t; break; }'
        source=base.replace_once(source,anchor,'      for (std::size_t t = 0; t < tracks_.size(); ++t) {\n        if (frameRecord.record) ++frameRecord.record->firstSteps;\n        if (!used[t] && (!tracks_[t].generation || tracks_[t].missed > 4)) { match = t; break; }\n      }')
        anchor='        for (std::size_t t = 0; t < tracks_.size(); ++t)\n          if (!used[t] && tracks_[t].missed > oldest) { oldest = tracks_[t].missed; match = t; }'
        source=base.replace_once(source,anchor,'        for (std::size_t t = 0; t < tracks_.size(); ++t) {\n          if (frameRecord.record) ++frameRecord.record->fallbackSteps;\n          if (!used[t] && tracks_[t].missed > oldest) { oldest = tracks_[t].missed; match = t; }\n        }')
    source=base.replace_once(source, '  // Partition every positive bin:',
        '  }\n  if (frameRecord.record) frameRecord.record->selected=static_cast<unsigned>(count_);\n  { '+scope(13)+'\n  // Partition every positive bin:')
    source=base.replace_once(source,'  previous_ = true;','  previous_ = true;\n  }')
    source=base.replace_once(source, 'std::arg(spectrum[k])', 'pog3_phase::phaseArg(spectrum[k],false)')
    source=base.replace_once(source, 'std::arg(previousSpectrum_[k])', 'pog3_phase::phaseArg(previousSpectrum_[k],true)')
    if 'previousPhaseValid_[k]' in source:
        source=base.replace_once(source, '      const float phase =',
            '      if (frameRecord.record && reusablePhase!=phaseReusable) pog3_phase::overflow=true;\n      const float phase =')
    (output/'PolyphonicPitchBank.cpp').write_text(source)
    bench=(output/'bench.cpp').read_text();anchor='  pog3_phase::report(name, static_cast<unsigned>(callback), times);'
    bench=base.replace_once(bench,anchor,'  pog3_phase::reportFrame(name, static_cast<unsigned>(callback));\n'+anchor)
    (output/'bench.cpp').write_text(bench)


if __name__=='__main__':
    main()
