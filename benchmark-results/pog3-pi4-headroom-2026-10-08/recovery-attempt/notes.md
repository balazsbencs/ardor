This superseded executable linked the NAM registrars correctly, but attempted
`snd_pcm_recover()` for each stream independently. The combined workload put
both streams into XRUN state. A read of `/proc/9028/stack` showed:

```
msleep
snd_rpi_iqaudio_post_dapm_event
dapm_seq_run
dapm_power_widgets
snd_soc_dapm_stream_event
__soc_pcm_prepare
soc_pcm_prepare
snd_pcm_do_prepare
snd_pcm_action_single
snd_pcm_action_nonatomic
snd_pcm_prepare
```

PID 9028 was terminated with SIGTERM; the runner's trap restored the application
(PID 9161 was independently read back). The failed combined run never reached
its result-writing stage, so its empty summary is not a zero-time result.
The final diagnostic stops on the first xrun and saves the completed lead-up.
Only the final root-level runs enter the headroom report.
