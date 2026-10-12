# POG3 recovery and endurance — 2026-10-12

Source `44f4c950`, same DSP algorithms and matched NEON seed as the integrated
app. `host-tests.log` records all 75 passing tests; host/Pi allocation logs test
actual worker stalls, exact delayed dry, restart/new targets, terminal bypass,
reset and healthy output equality at 32/64/128 frames. `ci-checks.json` records
all green checks on that code commit.

`long-run/` contains four individually passing steady samples totaling 600s,
actual selected presets, hardware settings, full clip/telemetry log and the
original runner's exit 1. Its scene request went to the wrong directory after
the steady samples; this is a retained harness failure. `scenes/` contains the
corrected short run, B/C/D/A scene recalls, local POG3 bypass/resume and exit 0.
The 600s result reaches 230,327 callbacks with zero callback/worker/DSP faults;
the corrected scene run reaches 18,503 with zero faults. Recorded input peaks
are −38.8 to −34.6 dBFS, so this is quiet-input hardware endurance rather than a
worst-case dense-playing qualification.

`run-endurance.sh` is the corrected reproducer; `run-scenes.sh` is the executed
short scene check. These require the temporary preset/model/settings copies,
which are excluded along with compiled binaries. `profiles.json` supplies POG3
normalized overrides; all six voice levels are 0.7 and master is 0.2. The
original compressor/Vibe/Supro NAM/EQ chain is retained. The scene fixture uses
100ms transitions and changes Attack, Focus, filter Q, expression position,
detune, Spread and local block enabled state. Test outputs are muted and the
normal app/codec/relay are restored by an exit trap.

`deploy-normal-app.sh`, `deployment-receipt.txt` and `deploy-return-code.txt`
record the installed matching app/manager binaries and verified output state,
unchanged presets/settings and read-only root. Only executables and the matched
FFTW seed/license are installed; test presets remain temporary. The backup path
is recorded in the receipt. Base image metadata remains unchanged.

See [implementation and measurement details](../../docs/pog3-recovery-results.md).
