#!/bin/sh
set -eu
repo=/home/bbalazs/projects/ardor-pog3
cache=/tmp/ardor-pog3-device-build
out=$repo/benchmark-results/pog3-pi4-phase-cache-2026-10-08/bulk-validity
remote=/tmp/pog3-phase-bulk-profile-20261008
ssh_opts='-o ControlPath=/tmp/ardor-pog3-profile-ssh -o BatchMode=yes'
mkdir -p "$out/diagnostic"
ssh $ssh_opts root@192.168.88.12 "mkdir -p '$remote'"
for build in baseline candidate; do
 scp -O $ssh_opts "$cache/phasebulk-profile-$build-arm/pog3-device-full" root@192.168.88.12:"$remote/profile-$build-bin"
done
scp -O $ssh_opts "$cache/phasebulk-baseline-arm/full-baseline" root@192.168.88.12:"$remote/full-baseline"
scp -O $ssh_opts "$cache/phasebulk-candidate-arm/pog3-device-cpu" "$out/shared.wisdom" "$out/run-remote.sh" "$out/profile-numerical" "$out/profile-baseline" "$out/profile-candidate" root@192.168.88.12:"$remote/"
ssh $ssh_opts root@192.168.88.12 "cd '$remote' && chmod +x profile-numerical profile-baseline profile-candidate && sha256sum profile-baseline-bin profile-candidate-bin full-baseline shared.wisdom" > "$out/diagnostic-target-binary-sha256.txt"
ssh $ssh_opts root@192.168.88.12 "sh '$remote/run-remote.sh' '$remote' 2 --full-probes profile-numerical profile-baseline profile-candidate" > "$out/diagnostic-runner.log" 2>&1
ssh $ssh_opts root@192.168.88.12 'pidof ardor-pedal' > "$out/diagnostic-service-readback.txt"
ssh $ssh_opts root@192.168.88.12 "cd '$remote' && gzip profile-baseline.log profile-candidate.log && rm full-baseline.f32 profile-baseline.f32 profile-candidate.f32"
scp -O $ssh_opts root@192.168.88.12:"$remote/*.txt" root@192.168.88.12:"$remote/*.csv" root@192.168.88.12:"$remote/*.log" root@192.168.88.12:"$remote/*.gz" root@192.168.88.12:"$remote/wisdom.*.after" root@192.168.88.12:"$remote/shared.wisdom" "$out/diagnostic/"
