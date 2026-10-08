#!/bin/sh
set -eu
repo=/home/bbalazs/projects/ardor-pog3
cache=/tmp/ardor-pog3-device-build
out=$repo/benchmark-results/pog3-pi4-interpretation-2026-10-08
remote=/tmp/pog3-frame-birth-20261008
ssh_opts='-o ControlPath=/tmp/ardor-pog3-profile-ssh -o BatchMode=yes'
for build in baseline candidate; do
  cp "$cache/frame-automation-$build-arm" "$cache/automation-$build"
  cp "$cache/frame-direct-$build-arm" "$cache/direct-$build"
  source="$cache/frame-$build-arm/pog3-device-full"
  if [ "$build" = baseline ]; then source="$cache/frame-baseline-arm/full-baseline"; fi
  cp "$source" "$cache/full-$build"
done
cp "$cache/frame-candidate-arm/pog3-device-pitch-quality" "$cache/pitch-candidate"
cp "$cache/frame-candidate-arm/pog3-device-frame-birth-slots" "$cache/frame-birth-test-arm"
cp "$cache/frame-profile-candidate-arm/pog3-device-full" "$cache/full-candidate-profile"
ssh $ssh_opts root@192.168.88.12 "mkdir -p '$remote'"
scp -O $ssh_opts "$cache/automation-baseline" "$cache/automation-candidate" "$cache/direct-baseline" "$cache/direct-candidate" "$cache/full-baseline" "$cache/full-candidate" "$cache/pitch-candidate" "$cache/full-candidate-profile" "$cache/pog3-device-cpu" "$out/shared.wisdom" "$out/run-remote.sh" "$out/numerical-candidate" "$out/profile-candidate" "$out/baseline-1" "$out/baseline-2" "$out/candidate-1" "$out/candidate-2" root@192.168.88.12:"$remote/"
scp -O $ssh_opts "$cache/frame-birth-test-arm" root@192.168.88.12:"$remote/frame-birth-test"
ssh $ssh_opts root@192.168.88.12 "cd '$remote' && chmod +x numerical-candidate profile-candidate baseline-1 baseline-2 candidate-1 candidate-2 && sha256sum full-baseline full-candidate full-candidate-profile automation-baseline automation-candidate direct-baseline direct-candidate pitch-candidate frame-birth-test shared.wisdom" > "$out/candidate-target-binary-sha256.txt"
ssh $ssh_opts root@192.168.88.12 "sh '$remote/run-remote.sh' '$remote' 2 --full-probes numerical-candidate baseline-1 candidate-1 candidate-2 baseline-2 profile-candidate" > "$out/candidate-runner.log" 2>&1
ssh $ssh_opts root@192.168.88.12 'pidof ardor-pedal' > "$out/candidate-service-readback.txt"
ssh $ssh_opts root@192.168.88.12 "cd '$remote' && gzip profile-candidate.log && rm automation-baseline.f32 automation-candidate.f32 direct-baseline.bin direct-candidate.bin full-baseline.f32 full-candidate.f32 full-candidate-profile.f32"
mkdir -p "$out/candidate-run"
scp -O $ssh_opts root@192.168.88.12:"$remote/*.txt" root@192.168.88.12:"$remote/*.csv" root@192.168.88.12:"$remote/*.log" root@192.168.88.12:"$remote/*.gz" root@192.168.88.12:"$remote/wisdom.*.after" "$out/candidate-run/"
