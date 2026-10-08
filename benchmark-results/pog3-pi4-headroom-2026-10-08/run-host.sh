#!/bin/sh
set -eu
out=/home/bbalazs/projects/ardor-pog3/benchmark-results/pog3-pi4-headroom-2026-10-08
remote=/tmp/pog3-headroom-20261008
opts='-o ControlPath=/tmp/ardor-pog3-profile-ssh -o BatchMode=yes'
scp -O $opts /tmp/ardor-pog3-device-build/headroom-arm/pedal-pog3-headroom root@192.168.88.12:$remote/probe
scp -O $opts "$out/run-remote.sh" root@192.168.88.12:$remote/run-remote.sh
result=0
ssh $opts root@192.168.88.12 "sh '$remote/run-remote.sh'" > "$out/runner.log" 2>&1 || result=$?
ssh $opts root@192.168.88.12 'pidof ardor-pedal' > "$out/service-readback.txt"
scp -O $opts root@192.168.88.12:"$remote/*.csv" root@192.168.88.12:"$remote/*.log" root@192.168.88.12:"$remote/*.txt" root@192.168.88.12:"$remote/*.maps" "$out/"
exit "$result"
