#!/bin/sh
set -eu
./live-baseline > live-baseline.f32 2> live-baseline.log
./live-candidate > live-profile.f32 2> live-profile.log
cmp live-baseline.f32 live-profile.f32
./full-baseline --freeze-trace shared.wisdom full-baseline.f32 > full-baseline.stdout 2> full-baseline.log
./full-profile --freeze-trace shared.wisdom full-profile.f32 > full-profile.stdout 2> full-profile.log
cmp full-baseline.f32 full-profile.f32
sha256sum live-baseline.f32 live-profile.f32 full-baseline.f32 full-profile.f32 > trace-sha256.txt
./pitch-profile > pitch-profile.log 2>&1
