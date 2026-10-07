#!/usr/bin/env python3
"""Generate coarse Attack substage diagnostics atop standalone callback profiling."""
from pathlib import Path
import sys
import callback_profile_sources as base


def main():
    root, output = map(Path, sys.argv[1:3])
    detail = len(sys.argv) == 4 and sys.argv[3] == "--detail"
    if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4 and not detail):
        raise RuntimeError("usage: attack_profile_sources.py root output [--detail]")
    sys.argv = sys.argv[:3]
    base.main()
    header = (output / 'callback_profile.h').read_text()
    header = base.replace_once(header, 'std::array<const char*,8> labels', 'std::array<const char*,14> labels')
    header = base.replace_once(header, '"long_attack", "short_attack", "long_render", "short_render"};',
        '"long_attack", "short_attack", "long_render", "short_render",\n'
        '  "attack_observations", "attack_group_scoring", "attack_group_families",\n'
        '  "attack_reservation", "attack_partials", "attack_canonical_index"};')
    header = base.replace_once(header, 'std::array<Total,8>', 'std::array<Total,14>')
    records = r'''
struct AttackRecord {
  std::uint64_t sample=0, frame=0;
  std::uint32_t resolution=0, control=0, left=0, right=0, observations=0,
                candidates=0, families=0, births=0;
};
inline std::array<AttackRecord,4096> attacks{};
inline std::size_t attackCount=0;
inline AttackRecord* currentAttack=nullptr;
'''
    header = base.replace_once(header, 'inline void reset() { enabled=false; overflow=false; count=0; beginSample(0); }',
        records + '''inline void reset() {
  enabled=false; overflow=false; count=attackCount=0; currentAttack=nullptr; beginSample(0);
  // Touch fixed diagnostic storage outside processing/timing, including pages
  // that were unused during the disabled warmup. Not production storage.
  events.fill(Event{}); attacks.fill(AttackRecord{});
}''')
    helper = r'''
struct AttackScope {
  bool active=false;
  AttackScope(std::size_t resolution, std::int64_t frame, float seconds,
              std::size_t left, std::size_t right):active(enabled) {
    if (!active) return;
    if (currentAttack || attackCount==attacks.size()) { overflow=true; active=false; return; }
    currentAttack=&attacks[attackCount++];
    *currentAttack={sample,static_cast<std::uint64_t>(frame),static_cast<std::uint32_t>(resolution),
      std::bit_cast<std::uint32_t>(seconds),static_cast<std::uint32_t>(left),static_cast<std::uint32_t>(right)};
  }
  ~AttackScope() { if (active) currentAttack=nullptr; }
};
inline void reportAttack(const char* workload, unsigned frames) {
  if (enabled || currentAttack || overflow) throw std::runtime_error("Attack profile bounds/state failed");
  std::ostringstream log;
  if (std::string_view(workload)=="matched_freeze_off") for (std::size_t i=0;i<attackCount;++i) {
    const auto& a=attacks[i];
    log<<"attack_counts,"<<workload<<','<<frames<<','<<a.sample<<','<<a.frame<<','
       <<a.resolution<<','<<a.control<<','<<a.left<<','<<a.right<<','<<a.observations<<','
       <<a.candidates<<','<<a.families<<','<<a.births<<'\n';
  }
  log<<"attack_receipt,"<<workload<<','<<frames<<','<<attackCount<<",overflow=0\n";
  std::cerr<<log.str();
}
'''
    header = base.replace_once(header, 'inline void report(const char* workload,', helper + '\ninline void report(const char* workload,')
    if detail:
        header = base.replace_once(header, 'candidates=0, families=0, births=0;','candidates=0, families=0, births=0;\n  std::array<std::uint64_t,4> microCalls{}, microInclusive{}, microExclusive{};')
        micro = r'''struct MicroScope {
  static inline MicroScope* current=nullptr;
  MicroScope* parent=nullptr;
  std::uint64_t child=0;
  unsigned stage=0;
  bool active=false;
  std::chrono::steady_clock::time_point start{};
  explicit MicroScope(unsigned value):stage(value),active(enabled && currentAttack) {
    if (!active) return;
    parent=current; current=this; start=std::chrono::steady_clock::now();
  }
  ~MicroScope() {
    if (!active) return;
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    ++currentAttack->microCalls[stage];
    currentAttack->microInclusive[stage]+=elapsed;
    currentAttack->microExclusive[stage]+=elapsed-child;
    current=parent; if(parent) parent->child+=elapsed;
  }
};
'''
        header = base.replace_once(header, 'inline void reportAttack(', micro+'inline void reportAttack(')
        anchor = "       <<a.candidates<<','<<a.families<<','<<a.births<<'\\n';"
        addition = r'''
    constexpr std::array<const char*,4> names{"owner","envelope","canonical","binding_birth_remainder"};
    for(unsigned j=0;j<4;++j) log<<"attack_micro,"<<workload<<','<<frames<<','<<a.sample<<','<<a.frame<<','
      <<a.resolution<<','<<names[j]<<','<<a.microCalls[j]<<','<<a.microInclusive[j]<<','<<a.microExclusive[j]<<'\n';
'''
        header = base.replace_once(header, anchor, anchor+addition)
    (output / 'callback_profile.h').write_text(header)
    source = (root / 'src/daisyfx/pog3/PolyphonicAttack.cpp').read_text()
    def scope(stage):
        return f'pog3_phase::Scope attackPhaseScope({stage}, 0, -1, -1, -1, std::bit_cast<std::uint32_t>(seconds_));'
    source = base.replace_once(source, '  std::size_t candidates = 0;\n  for (std::size_t p = 0;',
        '  std::size_t candidates = 0;\n  { ' + scope(9) + '\n  for (std::size_t p = 0;')
    source = base.replace_once(source, '  std::sort(candidates_.begin(), candidates_.begin() + candidates,',
        '  }\n  if (pog3_phase::currentAttack) pog3_phase::currentAttack->candidates=static_cast<std::uint32_t>(candidates);\n  ' + scope(10) + '\n  std::sort(candidates_.begin(), candidates_.begin() + candidates,')
    source = base.replace_once(source, '  rightUsed_.fill(false);',
        '  pog3_phase::AttackScope attackRecord(resolution, inputEnd, seconds_, l.size(), r.size());\n'
        '  std::size_t count = 0;\n  { ' + scope(8) + '\n  rightUsed_.fill(false);')
    source = base.replace_once(source, '  const float scale = 2.0f / left.frameSize();\n  std::size_t count = 0;',
        '  const float scale = 2.0f / left.frameSize();')
    source = base.replace_once(source, '  // Frame-end onset dates refer to input samples,',
        '  }\n  if (pog3_phase::currentAttack) pog3_phase::currentAttack->observations=static_cast<std::uint32_t>(count);\n  // Frame-end onset dates refer to input samples,')
    source = base.replace_once(source, '  used_.fill(false);\n  auto& partials',
        '  auto& partials')
    source = base.replace_once(source, '  reserved_.fill(false);',
        '  { ' + scope(11) + '\n  used_.fill(false);\n  reserved_.fill(false);')
    anchor = '  for (std::size_t p = 0; p < count; ++p) {\n    const auto& value = observations_[p];\n    std::size_t slot'
    source = base.replace_once(source, anchor, '  }\n  { ' + scope(12) + '\n' + anchor)
    source = base.replace_once(source, '    const bool birth = slot == partials.size();',
        '    const bool birth = slot == partials.size();\n    if (birth && pog3_phase::currentAttack) ++pog3_phase::currentAttack->births;')
    source = base.replace_once(source, '  indexCanonical(resolution);',
        '  }\n  { ' + scope(13) + '\n  indexCanonical(resolution);\n  }\n'
        '  if (pog3_phase::currentAttack) pog3_phase::currentAttack->families=static_cast<std::uint32_t>(familyCount());')
    if detail:
        anchor = '    std::size_t slot = partials.size();'
        source = base.replace_once(source, anchor, '    pog3_phase::MicroScope partialProbe(3);\n'+anchor)
        source = base.replace_once(source, '    auto* family = owner(value.frequency, inputEnd);',
            '    Family* family=nullptr;\n    { pog3_phase::MicroScope ownerProbe(0); family=owner(value.frequency, inputEnd); }')
        anchor = '    float gain = envelope(partial, value.magnitude, left.centerSamples(), inputEnd, sharedOnset, birth);'
        source = base.replace_once(source, anchor,
            '    float gain=0;\n    { pog3_phase::MicroScope envelopeProbe(1);\n'+anchor.replace('float gain =','gain =')+'\n    }')
        anchor = '    if (resolution != 2 && seconds_ > 0) {'
        source = base.replace_once(source, anchor, anchor+'\n      pog3_phase::MicroScope canonicalProbe(2);')
    (output / 'PolyphonicAttack.cpp').write_text('#include "callback_profile.h"\n' + source)
    bench = (output / 'bench.cpp').read_text()
    anchor = '  pog3_phase::report(name, static_cast<unsigned>(callback), times);'
    bench = base.replace_once(bench, anchor, '  pog3_phase::reportAttack(name, static_cast<unsigned>(callback));\n' + anchor)
    (output / 'bench.cpp').write_text(bench)


if __name__ == '__main__':
    main()
