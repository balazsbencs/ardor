#!/bin/sh
# Run from the staged audition directory on the pedal.
set -eu
kind=${1:-live}
seconds=${2:-900}
case "$kind" in live|silent) ;; *) echo 'usage: runner live|silent seconds' >&2; exit 1 ;; esac
mkdir .running || { echo 'An audition is already running.' >&2; exit 1; }
was_running=0
child=''
relay=/sys/class/leds/ardor:audio-output-enable/brightness
previous_relay=$(cat "$relay")
previous_headphone=$(amixer -c Zero cget 'name=Headphone Switch' | sed -n 's/^  : values=//p')
if pidof ardor-pedal > service-before.txt; then was_running=1; fi
cleanup() {
  result=$?
  trap - EXIT HUP INT TERM
  set +e
  echo 0 > "$relay"
  amixer -c Zero -q set 'Headphone' mute
  if [ -n "$child" ] && kill -0 "$child" 2>/dev/null; then
    kill -TERM "$child"
    wait "$child"
  fi
  if [ "$was_running" = 1 ]; then
    /etc/init.d/S99ardor-pedal start > service-restart.log 2>&1
    restored=0
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
      if pidof ardor-pedal > service-restored.txt; then restored=1; break; fi
      sleep 1
    done
    [ "$restored" = 1 ] || result=1
  else
    amixer -c Zero -q cset 'name=Headphone Switch' "$previous_headphone"
    echo "$previous_relay" > "$relay"
  fi
  printf '%s\n' "$result" > return-code.txt
  rmdir .running
  exit "$result"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
[ -n "$previous_headphone" ] || { echo 'Cannot read codec output mute state.' >&2; exit 1; }
rm -f command status.json status.tmp return-code.txt audition.pid
date -u > started.txt
sha256sum audition shared.wisdom > session-sha256.txt
if [ "$was_running" = 1 ]; then
  /etc/init.d/S99ardor-pedal stop > service-stop.log 2>&1
fi
if pidof ardor-pedal >/dev/null; then echo 'Normal audio service did not stop.' >&2; exit 1; fi
echo 0 > "$relay"
silent_arg=''
[ "$kind" = live ] || silent_arg=--silent
./audition shared.wisdom . "$seconds" $silent_arg > audition.log 2>&1 &
child=$!
printf '%s\n' "$child" > audition.pid
if [ "$kind" = live ]; then
  ready=0
  for attempt in $(seq 1 100); do
    if grep -q '"ready":1' status.json 2>/dev/null; then ready=1; break; fi
    kill -0 "$child" 2>/dev/null || break
    sleep .1
  done
  [ "$ready" = 1 ] || { echo 'Audition failed to open audio.' >&2; exit 1; }
  # Stopping the normal supervisor mutes the codec as well as the relay.
  # PCM activity and nonzero DSP output alone do not prove audible output.
  amixer -c Zero -q set 'Headphone' unmute
  amixer -c Zero cget 'name=Headphone Switch' > codec-output-live.txt
  grep -q ': values=on,on' codec-output-live.txt || { echo 'Codec output stayed muted.' >&2; exit 1; }
  echo 1 > "$relay"
fi
wait "$child"
