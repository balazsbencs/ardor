#!/bin/sh
set -eu
out=/home/bbalazs/projects/ardor-pog3/benchmark-results/pog3-pi4-phase-cache-2026-10-08/bulk-validity
remote=/tmp/pog3-phase-bulk-20261008
ssh_opts='-o ControlPath=/tmp/ardor-pog3-profile-ssh -o BatchMode=yes'
mkdir -p "$out/confirmation"
scp -O $ssh_opts "$out/shared.wisdom" root@192.168.88.12:"$remote/shared.wisdom"
ssh $ssh_opts root@192.168.88.12 "sh '$remote/run-remote.sh' '$remote' 2 --full-probes baseline-1 candidate-1 candidate-2 baseline-2" > "$out/confirmation-runner.log" 2>&1
ssh $ssh_opts root@192.168.88.12 'pidof ardor-pedal' > "$out/confirmation-service-readback.txt"
scp -O $ssh_opts root@192.168.88.12:"$remote/device-*.txt" root@192.168.88.12:"$remote/restored-service-pid.txt" root@192.168.88.12:"$remote/receipt.csv" root@192.168.88.12:"$remote/baseline-*.csv" root@192.168.88.12:"$remote/candidate-*.csv" root@192.168.88.12:"$remote/baseline-*.log" root@192.168.88.12:"$remote/candidate-*.log" root@192.168.88.12:"$remote/service-*.log" root@192.168.88.12:"$remote/wisdom.baseline-*.after" root@192.168.88.12:"$remote/wisdom.candidate-*.after" root@192.168.88.12:"$remote/shared.wisdom" "$out/confirmation/"
