# Live audition output correction

The user reported silence and no audible footswitch changes in the first live
attempt. `first-muted-status.json` records its final DSP status; mode changes
were also present in the device log. These counters did not prove audible output.

The installed service's shutdown trap mutes `Headphone`. The original audition
runner enabled the relay but omitted the codec unmute. The runner now unmutes
and reads back both headphone switches before enabling the relay, and mutes the
codec during cleanup. No production DSP, installed app or saved settings changed.

The first attempt was stopped cleanly (exit 0) and the normal app restored before
the corrected runner was staged. `corrected-live-readback.txt` captures the
restarted dry audition, codec switch values, relay brightness and runner hash.
This is output-state verification; audible confirmation and playing feel still
require user feedback. Shell syntax and `git diff --check` passed.
