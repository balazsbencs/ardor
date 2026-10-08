#!/bin/sh
# Execute only from the staged device directory. Writes silence to hardware.
set -eu
test ! -d .running
rm -f status.json command
(
  ready=0
  for attempt in $(seq 1 100); do
    if grep -q '"ready":1' status.json 2>/dev/null; then ready=1; break; fi
    sleep .1
  done
  [ "$ready" = 1 ]
  for mode in up down blend focus-on; do
    test -d .running
    printf '%s\n' "$mode" > command.tmp
    mv command.tmp command
    sleep 1
    cat status.json
  done
) > silent-control-trace.txt 2>&1 &
control=$!
sh run.sh silent 12
wait "$control"
