#!/bin/sh
# Control the prepared, temporary audition. SSH authentication is external.
set -eu
target=${POG3_AUDITION_HOST:-root@192.168.88.12}
session=/tmp/pog3-audition-20261008
socket=${POG3_AUDITION_SSH_SOCKET:-/tmp/ardor-pog3-audition-ssh}
action=${1:-status}
case "$action" in
  start)
    ssh -o ControlPath="$socket" "$target" \
      "cd '$session' && test ! -d .running && (nohup sh run.sh live 900 > runner.log 2>&1 < /dev/null &)"
    ;;
  dry|up|down|blend|focus-on|focus-off|stop)
    ssh -o ControlPath="$socket" "$target" \
      "cd '$session' && test -d .running && printf '%s\n' '$action' > command.tmp && mv command.tmp command"
    ;;
  status)
    ssh -o ControlPath="$socket" "$target" \
      "cd '$session' && if test -d .running; then cat status.json; else printf 'Audition inactive; normal pedal PID: '; pidof ardor-pedal; fi"
    ;;
  *) echo 'usage: pog3-audition.sh start|status|dry|up|down|blend|focus-on|focus-off|stop' >&2; exit 1 ;;
esac
