#!/bin/sh
set -eu
repo=/home/bbalazs/projects/ardor-pog3
cache=/tmp/ardor-pog3-device-build
out=$repo/benchmark-results/pog3-pi4-interpretation-2026-10-08
remote=/tmp/pog3-interpretation-profile-20261008
ssh_opts='-o ControlPath=/tmp/ardor-pog3-profile-ssh -o BatchMode=yes'
"$cache/frame-baseline-host/full-baseline" --freeze-trace "$cache/phase-host-shared.wisdom" "$cache/frame-host-baseline.f32" 2> "$out/host-baseline.log"
"$cache/frame-profile-host/pog3-device-full" --freeze-trace "$cache/phase-host-shared.wisdom" "$cache/frame-host-profile.f32" 2> "$out/host-profile.log"
cmp "$cache/frame-host-baseline.f32" "$cache/frame-host-profile.f32"
sha256sum "$cache/frame-host-baseline.f32" "$cache/frame-host-profile.f32" > "$out/host-trace-sha256.txt"
wc -c "$cache/frame-host-baseline.f32" "$cache/frame-host-profile.f32" > "$out/host-trace-bytes.txt"
cp "$cache/frame-baseline-arm/full-baseline" "$cache/full-baseline"
cp "$cache/frame-profile-arm/pog3-device-full" "$cache/full-profile"
cp "$cache/frame-profile-arm/pog3-device-cpu" "$cache/pog3-device-cpu"
cp "$repo/benchmark-results/pog3-pi4-attack-gain-2026-10-07/shared.wisdom" "$out/shared.wisdom"
cp "$repo/benchmark-results/pog3-pi4-attack-gain-2026-10-07/run-remote.sh" "$out/run-remote.sh"
cat > "$out/numerical" <<'INNER'
#!/bin/sh
set -eu
for probe in baseline profile; do
  ./full-$probe --freeze-trace shared.wisdom full-$probe.f32 > full-$probe.stdout 2> full-$probe.log
done
cmp full-baseline.f32 full-profile.f32
sha256sum full-baseline.f32 full-profile.f32 > trace-sha256.txt
wc -c full-baseline.f32 full-profile.f32 > trace-bytes.txt
INNER
cat > "$out/profile" <<'INNER'
#!/bin/sh
set -eu
./full-profile --freeze-matched forward shared.wisdom
cp shared.wisdom wisdom.profile.after
INNER
chmod +x "$out/numerical" "$out/profile"
ssh $ssh_opts root@192.168.88.12 "mkdir -p '$remote'"
scp -O $ssh_opts "$cache/full-baseline" "$cache/full-profile" "$cache/pog3-device-cpu" "$out/shared.wisdom" "$out/run-remote.sh" "$out/numerical" "$out/profile" root@192.168.88.12:"$remote/"
ssh $ssh_opts root@192.168.88.12 "cd '$remote' && sha256sum full-baseline full-profile pog3-device-cpu shared.wisdom" > "$out/target-binary-sha256.txt"
ssh $ssh_opts root@192.168.88.12 "sh '$remote/run-remote.sh' '$remote' 2 --full-probes numerical profile" > "$out/runner.log" 2>&1
ssh $ssh_opts root@192.168.88.12 'pidof ardor-pedal' > "$out/service-readback.txt"
ssh $ssh_opts root@192.168.88.12 "cd '$remote' && gzip profile.log && rm full-baseline.f32 full-profile.f32"
scp -O $ssh_opts root@192.168.88.12:"$remote/*.txt" root@192.168.88.12:"$remote/*.csv" root@192.168.88.12:"$remote/*.log" root@192.168.88.12:"$remote/*.gz" root@192.168.88.12:"$remote/wisdom.profile.after" "$out/"
