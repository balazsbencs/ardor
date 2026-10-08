#!/bin/sh
set -eu
repo=/home/bbalazs/projects/ardor-pog3
cache=/tmp/ardor-pog3-device-build
out=$repo/benchmark-results/pog3-pi4-phase-cache-2026-10-08
remote=/tmp/pog3-phase-cache-20261008
ssh_opts='-o ControlPath=/tmp/ardor-pog3-profile-ssh -o BatchMode=yes'
for build in baseline candidate; do
  cp "$cache/phasecache-automation-$build-arm" "$cache/phasecache-stage-automation-$build"
  cp "$cache/phasecache-direct-$build-arm" "$cache/phasecache-stage-direct-$build"
  source="$cache/phasecache-$build-arm/pog3-device-full"
  if [ "$build" = baseline ]; then source="$cache/phasecache-baseline-arm/full-baseline"; fi
  cp "$source" "$cache/phasecache-stage-full-$build"
done
ssh $ssh_opts root@192.168.88.12 "mkdir -p '$remote'"
for build in baseline candidate; do
  scp -O $ssh_opts "$cache/phasecache-stage-automation-$build" root@192.168.88.12:"$remote/automation-$build"
  scp -O $ssh_opts "$cache/phasecache-stage-direct-$build" root@192.168.88.12:"$remote/direct-$build"
  scp -O $ssh_opts "$cache/phasecache-stage-full-$build" root@192.168.88.12:"$remote/full-$build"
done
scp -O $ssh_opts "$cache/phasecache-candidate-arm/pog3-device-pitch-quality" root@192.168.88.12:"$remote/pitch-candidate"
scp -O $ssh_opts "$cache/phasecache-candidate-arm/pog3-device-cpu" "$out/shared.wisdom" "$out/run-remote.sh" "$out/numerical" "$out/baseline-1" "$out/baseline-2" "$out/candidate-1" "$out/candidate-2" root@192.168.88.12:"$remote/"
ssh $ssh_opts root@192.168.88.12 "cd '$remote' && chmod +x full-baseline numerical baseline-1 baseline-2 candidate-1 candidate-2 && sha256sum full-baseline full-candidate automation-baseline automation-candidate direct-baseline direct-candidate pitch-candidate pog3-device-cpu shared.wisdom" > "$out/target-binary-sha256.txt"
ssh $ssh_opts root@192.168.88.12 "sh '$remote/run-remote.sh' '$remote' 2 --full-probes numerical baseline-1 candidate-1 candidate-2 baseline-2" > "$out/runner.log" 2>&1
ssh $ssh_opts root@192.168.88.12 'pidof ardor-pedal' > "$out/service-readback.txt"
ssh $ssh_opts root@192.168.88.12 "cd '$remote' && rm automation-baseline.f32 automation-candidate.f32 direct-baseline.bin direct-candidate.bin full-baseline.f32 full-candidate.f32"
scp -O $ssh_opts root@192.168.88.12:"$remote/*.txt" root@192.168.88.12:"$remote/*.csv" root@192.168.88.12:"$remote/*.log" root@192.168.88.12:"$remote/wisdom.*.after" "$out/"
