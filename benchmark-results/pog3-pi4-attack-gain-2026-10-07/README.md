# Rejected deferred Attack gain experiment

Production remains at base `4de167c00694b53bd4e61d8fdba80659958d22a7`.
Only standalone diagnostic counters/scope handling and documentation are retained.
Read [the report](../../docs/pog3-attack-gain-results.md) for rejection, ordering
review and the next primary-interpretation plan.

- Decompress and apply `candidate.patch.gz` to base source to reproduce the
  rejected gain split. `diagnostic-harness.patch.gz` supplies instrumentation.
  It supports both baseline and candidate. Build each automation probe with its
  matching source headers. Immutable libraries/ELFs are not committed.
- `build-host.sh`, `build-arm.sh`, `host-numerical.sh`, `host-validation.sh` and
  `build-context.txt` retain exact local paths/mounts/toolchains; adapt paths in
  a new environment. `candidate-source-input-sha256.txt` describes the measured
  prototype, which is deliberately different from final production source.
  `restored-source-check.txt` and `restored-arm-elf-check.txt` verify restoration.
- `diagnostic/` contains the successful numerical/pitch gate and the two
  diagnostic runs. Its recorded durations are excluded from ordinary CPU results.
  `check_gain_counts.py` audits per-update work and regenerates the count summary.
  `summarize_phases.py diagnostic counts-baseline` (or counts-candidate) regenerates
  stage accounting from compressed logs. Prefix each command with `python3`.
- Root and `confirmation/` contain the independent ordinary ABBA rounds. Run
  `python3 summarize.py .`, `python3 summarize.py confirmation` and
  `python3 summarize_confirmation.py .` to regenerate CPU summaries. Use
  `python3 check_wisdom.py .`, `python3 check_wisdom.py confirmation` and
  `python3 check_wisdom.py diagnostic 2` to audit the four/four/two exports.
- `generated-source-check.txt` verifies all six generated files in four modes:
  baseline/candidate × coarse/detail. Nested diagnostics are compile-checked;
  recorded actual-work counts come from coarse diagnostic builds.
- `sha256sum -c artifact-sha256.txt` audits retained artifacts recursively.
  Float blobs, executables and local compiler caches are not committed.

No firmware/application deployment, configuration, buffer, governor, NAM quality
or original-checkout/index changes are made. Services are restored/read back and
unique probe files/host comparison blobs removed after retrieval.
