#!/bin/sh
set -eu
./full-baseline --freeze-wisdom shared.wisdom > wisdom-generation.log 2>&1
./live-baseline > live-baseline.f32 2> live-baseline.log
./live-candidate > live-candidate.f32 2> live-candidate.log
cmp live-baseline.f32 live-candidate.f32
./full-baseline --freeze-trace shared.wisdom full-baseline.f32 > full-baseline.stdout 2> full-baseline.log
./full-candidate --freeze-trace shared.wisdom full-candidate.f32 > full-candidate.stdout 2> full-candidate.log
cmp full-baseline.f32 full-candidate.f32
sha256sum live-baseline.f32 live-candidate.f32 full-baseline.f32 full-candidate.f32 > trace-sha256.txt
./pitch-candidate > pitch-candidate.log 2>&1
