# Companion trainer A2 reference

`parametric-trainer-a2.json` contains four complete model documents, 512 input
samples and 19 audio references. The first three documents are real one-epoch
CPU exports from `/home/bbalazs/projects/parametric` (the companion capture app):

- A2 Standard: 8 channels, one continuous control, amp metadata.
- A2 Nano: 3 channels, one continuous control, amp metadata.
- Current capture recipe: 10 channels, one continuous control and a named
  binary switch, pedal metadata.
- A2 Nano contract fixture: 3 channels and 24 continuous controls, pedal
  metadata. This last network is initialized, **not trained**; it tests the
  capacity limit and weight/channel encoding rather than audio quality.

The first two are produced by `tests/test_architectures.py`; the switch model
comes from `tests/test_controls.py`. Training uses synthetic WAVs. These
fixtures establish compatibility, not hardware capture accuracy.

The trainer uses Phillip Self's MIT-licensed fork at
`a3684411076be69dab53505df14922487697f2cf`. Reference generation reloads the
exported weights with that fork's Python `ConcatWaveNet`, then evaluates
`pad_start=True`. The reference includes Ardor's existing loudness normalization
(`10 ** ((-18 - metadata.loudness) / 20)` when loudness is present), without input
calibration. Definitions, weights and metadata remain exactly as exported.

Cases include defaults, the first control's minimum/maximum, all controls at
minimum/maximum, the last control's maximum and switch maxima when applicable.
The C++ test uses both 13- and 64-frame buffers and an absolute tolerance of
`5e-5`. It also verifies native asset discovery, every control and the amp/pedal
category, and rejects a 25th concat control and a 17th FiLM control. Go asset-store
tests upload the original documents and check metadata and capacity rejection.

To regenerate, run the trainer suite in its installed environment, retaining
the test directories:

```sh
.venv/bin/pytest --basetemp=/tmp/ardor-trainer-reference
```

For the initialized 24-control contract export, run the following Python with
the trainer's `.venv/bin/python` from the trainer repository:

```python
import json
from pathlib import Path
import torch
from nam.capture.params import KnobSpec
from nam.models.parametric import ConcatWaveNet, export_parametric
from parametric_mac.controls import ControlProject, ControlModel, model_config
from parametric_mac.training import finish_exports

torch.set_num_threads(2)
torch.manual_seed(14515)
project = ControlProject(name="24-control contract fixture", sample_rate=48000,
    knobs=[ControlModel.from_spec(KnobSpec(f"Control {i+1}", 0, 10, 1)) for i in range(24)])
net = ConcatWaveNet.init_from_config(model_config(project, "a2-nano")["net"]["config"])
out = Path("/tmp/ardor-trainer-24-controls")
out.mkdir(exist_ok=True)
export_parametric(net, out, basename="model_parametric")
(out / "manifest.json").write_text(json.dumps({"model_name": project.name,
    "gear_type": "pedal", "model_architecture": "a2-nano"}))
finish_exports(out)
```

Then pass the Standard, Nano, switch and 24-control exports to the trainer's
reference generator:

```sh
.venv/bin/python scripts/export-ardor-reference.py \
  /path/to/standard/model_parametric.nam \
  /path/to/nano/model_parametric.nam \
  /path/to/switch/model_parametric.nam \
  /tmp/ardor-trainer-24-controls/model_parametric.nam \
  --output ../ardor/tests/fixtures/parametric-trainer-a2.json
```

The fixture was generated with Python 3.13.5 and CPU PyTorch 2.14.1. Retraining
can change weights across versions or execution order; regenerate the audio
references from the newly exported weights together.
