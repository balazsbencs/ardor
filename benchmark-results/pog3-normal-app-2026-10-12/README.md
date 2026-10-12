# Normal POG3 app hardware check — 2026-10-12

The full fbdev app ran the actual device's serial Vibe + Supro NAM + EQ chain,
with one POG3 prepended, in a temporary data root. Four 30-second checks cover
dry/up/down/blend and runtime preset replacement. No saved preset or global
settings file was replaced. `hw-params.txt` records the actual 48k/128/384 MMAP
stereo backend; `*.performance.txt`, `*.start.txt`, `*.end.txt` and `*.active.json`
record each sample and its selected preset. `return-code.txt` is 0. The final
telemetry has 47,455 callbacks with zero overruns/gaps/worker misses/DSP faults.

`target-quality.log` compares hosted output to the direct core at exactly
256-sample delay for 32/64/128 frames, with input-dated controls and the global
libc allocation/free check enabled. All worst errors and allocation/free counts
are zero. The performance script verifies actual CPU2 FIFO70/CPU3 FIFO69
scheduling and faults, not just average CPU usage. Core CPU utilization does not
measure the worker's per-job execution duration.

The first muted attempt was interrupted by a bank change to an empty preset;
its missing-worker check failed correctly. These are the repeated **unmuted**
passing samples. User feedback afterward: “I think it sounds great.” Both codec
and relay were enabled during the repeat. This is subjective acceptance in this
fixture, including the new worker delay, rather than certification of all chains
or every control endpoint. Private settings/models and compiled binaries are
excluded from this directory. The deployment receipt identifies the installed
source/binary hashes and output state separately from this pre-deploy run.

See [integration details and limits](../../docs/pog3-normal-app-integration.md).
