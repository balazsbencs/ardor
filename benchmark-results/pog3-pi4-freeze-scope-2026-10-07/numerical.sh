#!/bin/sh
set -eu
./full-enabled --freeze-wisdom shared.wisdom > wisdom-generation.log 2>&1
./full-enabled --freeze-trace shared.wisdom trace-enabled.f32 > trace-enabled.stdout 2> trace-enabled.log
./full-disabled --freeze-trace shared.wisdom trace-disabled.f32 > trace-disabled.stdout 2> trace-disabled.log
cmp trace-enabled.f32 trace-disabled.f32
sha256sum trace-enabled.f32 trace-disabled.f32 > trace-sha256.txt
./pitch-disabled > pitch-disabled.log 2>&1
