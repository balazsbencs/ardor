# Raspberry Pi 4 parametric NAM evidence

Read the [validation report](../../docs/parametric-nam-pi4-validation-2026-10-11.md) for interpretation, capacity recommendations, method and limits.

- `model-sweep.csv`: single-model ARM compute measurements, 102 cases.
- `chain-*.csv`, `qualification-*.csv`, `mixed-*.csv`: full-engine serial and pedal/amp/cab measurements; command files retain invocations. Deliberately oversized cases are included.
- `extended-*`: unattended wall-clock duplex ALSA runs. Each directory retains the requested active preset, native audio parameters, timing telemetry, kernel XRUN trace, CPU/scheduling probe and memory snapshots.
- `resources.csv`: thirty-second process RSS/HWM samples during extended runs. `thermal.csv` samples board temperature, frequency and undervoltage alarms every five seconds.
- `api-results.json`, `browser-results.json`, `amp-*-results.json`: actual Pi manager integration. The browser tests use the real API without mocks.
- `listening/`: user-reported gain/tone comparison, callback telemetry and XRUN trace. No analog loopback or calibrated analyzer was available.
- `public-model-catalog.json`: release URLs, hashes and schemas of twelve public Gene Ko models. `public-model-host-format-*` is an x86_64 load/format smoke across all 27 tiers and is excluded from Pi timing totals. `public-model-arm-sweep.csv` adds the actual Pi single-processor measurements: 54 cases, 162,000 blocks.
- `binaries.sha256`: original measurement binaries. `quiet-controls-binaries.sha256` and `quiet-controls-arm-smokes.log`: the later native UI fixes, verified separately.
- `restoration*`, `guardian-extended.log`, `unattended-status.json`: final restoration and completion outcomes. These appear after the extended suite completes.

Initial fixture/launch/API failures are retained with their original names and explained in the report. Cases containing `invalid-fixture`, `failed-control-open` or `control-interruption`, and the interrupted `soak-snapshot-two`, are not complete static-chain qualifications. `validation-status.txt` verifies fixture and active-preset integrity; it does not override an audio-health failure in `performance.txt`. Kernel XRUNs, processing overruns, callback gaps and latched bypass are distinct counters.

No credentials, private user model weights, recorded user audio, or downloaded public model weights are included. Resident private captures stayed on the Pi. The synthetic cabinet is a reproducible compute workload, not a measured real cabinet response.

`finish-unattended.py` preserves this session's completion procedure and uses its isolated temporary paths. The host completion process stopped during the overnight pause; the remote suite completed independently, and its safety guardian restored production at 02:07 UTC. Final model checks, collection and restoration verification were completed manually after resuming. The report generator was corrected to include worker faults before it was used. The original WDW probe says PASS but omitted nine worker underflows during its sample; the report reclassifies that run as FAIL. It collects evidence, restores the installed service, verifies saved-file hashes, builds the final report and updates the existing PR. Do not run mutation helpers against production data roots or an unrelated device.
