Ardor Pedal Data Partition Layout
==================================

This partition is mounted read-write at /opt/ardor-pedal.

Directory layout:
  models/       — NAM model files (*.nam), 48 kHz
  irs/           — Cabinet impulse responses (*.wav), 48 kHz mono
  presets/bank-000/
    preset-0.json
    preset-1.json
    preset-2.json
    preset-3.json

Default presets (shipped with the image) are pass-through (empty block chain).
Replace them by editing the JSON files directly or using the UI Save button.

Asset paths inside preset JSON are relative to this directory.
Example:
  "asset": "models/my-amp.nam"
  "asset": "irs/my-cab.wav"

No user-provided NAM models or IR files ship in the firmware image.
Copy assets over SSH or mount the partition on a host machine.
