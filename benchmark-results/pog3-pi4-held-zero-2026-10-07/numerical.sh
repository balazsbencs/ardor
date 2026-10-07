#!/bin/sh
set -eu
./trace-baseline > trace-baseline.f32 2> trace-baseline.log
./trace-candidate > trace-candidate.f32 2> trace-candidate.log
cmp trace-baseline.f32 trace-candidate.f32
sha256sum trace-baseline.f32 trace-candidate.f32 > trace-sha256.txt
./pog3-device-pitch-quality > pitch-quality.log 2>&1
