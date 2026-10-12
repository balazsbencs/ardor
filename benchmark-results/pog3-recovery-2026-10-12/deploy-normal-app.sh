#!/bin/sh
set -eu
stage=/tmp/pog3-recovery-deploy-20261012
backup=/opt/ardor-pedal/pog3-deploy-backup-20261012-44f4c950
cd "$stage"
[ ! -e "$backup" ] || { echo 'Backup already exists; inspect before retry.'; exit 1; }
[ ! -x /opt/ardor-pedal/system/current/bin/ardor-pedal ]
[ ! -x /opt/ardor-pedal/system/current/bin/ardor-managerd ]
./ardor-pedal --version
./ardor-managerd --version
mkdir "$backup"
cp /usr/bin/ardor-pedal "$backup/ardor-pedal"
cp /usr/bin/ardor-managerd "$backup/ardor-managerd"
sha256sum "$backup/ardor-pedal" "$backup/ardor-managerd" > "$backup/before-binaries.sha256"
(cd /opt/ardor-pedal && find presets settings -type f -exec sha256sum '{}' ';' | sort) > "$backup/before-data.sha256"
changed=0
root_rw=0
success=0
cleanup() {
  code=$?
  trap - EXIT HUP INT TERM
  set +e
  if [ "$success" != 1 ] && [ "$changed" = 1 ]; then
    echo 'Candidate failed verification; restoring previous app and manager.'
    /etc/init.d/S98ardor-managerd stop
    /etc/init.d/S99ardor-pedal stop
    mount -o remount,rw /
    cp "$backup/ardor-pedal" /usr/bin/ardor-pedal.pog3-rollback
    cp "$backup/ardor-managerd" /usr/bin/ardor-managerd.pog3-rollback
    chmod 755 /usr/bin/ardor-pedal.pog3-rollback /usr/bin/ardor-managerd.pog3-rollback
    mv /usr/bin/ardor-pedal.pog3-rollback /usr/bin/ardor-pedal
    mv /usr/bin/ardor-managerd.pog3-rollback /usr/bin/ardor-managerd
    sync
    mount -o remount,ro /
    /etc/init.d/S99ardor-pedal start
    /etc/init.d/S98ardor-managerd start
  elif [ "$root_rw" = 1 ]; then
    sync
    mount -o remount,ro /
  fi
  printf '%s\n' "$code" > "$stage/deploy-return-code.txt"
  exit "$code"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
/etc/init.d/S98ardor-managerd stop
/etc/init.d/S99ardor-pedal stop
changed=1
mount -o remount,rw /
root_rw=1
cp ardor-pedal /usr/bin/ardor-pedal.pog3-new
cp ardor-managerd /usr/bin/ardor-managerd.pog3-new
chmod 755 /usr/bin/ardor-pedal.pog3-new /usr/bin/ardor-managerd.pog3-new
mv /usr/bin/ardor-pedal.pog3-new /usr/bin/ardor-pedal
mv /usr/bin/ardor-managerd.pog3-new /usr/bin/ardor-managerd
mkdir -p /usr/share/ardor-pedal /usr/share/licenses/fftw-single
cp pog3-fftw.wisdom /usr/share/ardor-pedal/pog3-fftw.wisdom
cp fftw-COPYING /usr/share/licenses/fftw-single/COPYING
sync
mount -o remount,ro /
root_rw=0
rm -f /run/ardor-pedal.telemetry
/etc/init.d/S99ardor-pedal start
/etc/init.d/S98ardor-managerd start
ready=0
for attempt in $(seq 1 30); do
  if grep -q 'callbacks=[1-9]' /run/ardor-pedal.telemetry 2>/dev/null && pidof ardor-managerd >/dev/null; then ready=1; break; fi
  sleep 1
done
[ "$ready" = 1 ]
sleep 3
pid=$(pidof ardor-pedal)
manager_pid=$(pidof ardor-managerd)
[ "$(readlink /proc/$pid/exe)" = /usr/bin/ardor-pedal ]
[ "$(readlink /proc/$manager_pid/exe)" = /usr/bin/ardor-managerd ]
[ "$(sha256sum ardor-pedal | cut -d ' ' -f1)" = "$(sha256sum /usr/bin/ardor-pedal | cut -d ' ' -f1)" ]
[ "$(sha256sum ardor-managerd | cut -d ' ' -f1)" = "$(sha256sum /usr/bin/ardor-managerd | cut -d ' ' -f1)" ]
amixer -c Zero cget 'name=Headphone Switch' > codec-output.txt
grep -q 'values=on,on' codec-output.txt
[ "$(cat /sys/class/leds/ardor:audio-output-enable/brightness)" != 0 ]
wget -qO manager-index.html http://127.0.0.1:8080/
grep -q 'index-B74KFEzX.js' manager-index.html
wget -qO manager-app.js http://127.0.0.1:8080/assets/index-B74KFEzX.js
grep -q 'Poly Octave 3' manager-app.js
(cd /opt/ardor-pedal && find presets settings -type f -exec sha256sum '{}' ';' | sort) > "$backup/after-data.sha256"
cmp "$backup/before-data.sha256" "$backup/after-data.sha256"
{
  echo 'source_commit=44f4c950'
  echo "backup=$backup"
  echo "audio_pid=$pid manager_pid=$manager_pid"
  /usr/bin/ardor-pedal --version
  /usr/bin/ardor-managerd --version
  sha256sum /usr/bin/ardor-pedal /usr/bin/ardor-managerd /usr/share/ardor-pedal/pog3-fftw.wisdom
  cat /run/ardor-pedal.telemetry
  cat /proc/asound/card0/pcm0p/sub0/hw_params
  cat codec-output.txt
  printf 'relay='; cat /sys/class/leds/ardor:audio-output-enable/brightness
  echo 'presets_and_settings=unchanged'
  awk '$2 == "/" {print "root_mount=" $4}' /proc/mounts
  echo 'manager_catalog=Poly Octave 3 available'
} > deployment-receipt.txt
cat deployment-receipt.txt
success=1
