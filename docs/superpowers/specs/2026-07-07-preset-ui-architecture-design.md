# Preset And UI Architecture Design

## Goal

Define the first preset, effect-chain, and UI architecture for the Raspberry Pi guitar pedal so the LVGL UI, future HTML mockups, and audio engine can share one simple model.

This phase does not implement the UI. It defines the contract that the next implementation phases should follow.

## Product Scope

The first UI-capable pedal version has:

- One 5 inch touch display.
- One encoder.
- Four footswitches.
- Preset mode for performance.
- Edit mode for changing the active preset.
- Encoder-controlled master output volume.
- One serial signal chain.
- Manual preset save and discard.
- Live audio preview while editing.

Routing stays serial in this phase. Split, parallel, and stereo dual-chain routing are later phases.

## Preset Model

The pedal has 100 banks with 4 presets per bank, for 400 preset slots.

Presets are stored as separate JSON files under the pedal data root:

```text
presets/bank-000/preset-0.json
presets/bank-000/preset-1.json
presets/bank-000/preset-2.json
presets/bank-000/preset-3.json
presets/bank-001/preset-0.json
```

Each preset stores the complete serial chain:

```json
{
  "version": 1,
  "name": "Clean Lead",
  "routing": "serial",
  "global": {
    "inputGainDb": -12.0,
    "outputGainDb": -6.0,
    "safetyLimitDb": -1.0
  },
  "blocks": [
    {
      "id": "block-1",
      "type": "nam",
      "enabled": true,
      "asset": "models/clean.nam",
      "params": {
        "levelDb": 0.0
      }
    },
    {
      "id": "block-2",
      "type": "cab",
      "enabled": true,
      "asset": "irs/open-back.wav",
      "params": {
        "mix": 1.0,
        "levelDb": 0.0
      }
    }
  ]
}
```

Rules:

- `version` is required and starts at `1`.
- `routing` is required and must be `"serial"` for this phase.
- `blocks` is an ordered array. Reordering blocks changes processing order.
- There is no fixed block-count limit in the preset file.
- Block `id` values are stable across reorder, save, and load.
- Block `asset` values are relative paths, such as `models/foo.nam` and `irs/bar.wav`.
- Assets should have a human-readable name for UI lists, separate from the file path.
- The first implemented block types are `nam` and `cab`.
- Future block types may be stored before the engine supports them.

The model intentionally allows unusual orders, such as cab before NAM. The UI should not enforce a fixed amp/cab order.

## Working Preset State

The runtime keeps two versions of the current preset:

```text
saved preset
working preset
```

Edit mode changes the working preset immediately. The player hears those changes immediately.

Save writes the working preset to the preset file. Discard reloads the saved preset from disk and rebuilds the chain.

The UI shows a dirty state when the working preset differs from the saved preset.

Preset writes must be atomic:

```text
write preset-0.json.tmp
flush/close
replace preset-0.json
```

## Master Output Volume

The encoder controls the pedal's master output volume.

This is a system-level volume, not a preset parameter:

- Turning the encoder does not mark the active preset dirty.
- Changing preset does not reset master volume.
- Master volume is applied after the active chain or dry bypass path.
- Master volume is applied before the final safety limiter.

Preset `global.outputGainDb` remains the preset's saved level trim. Master volume is the physical output level control for the whole pedal.

## Engine Chain Contract

The audio engine consumes the working preset.

Structural edits rebuild the serial chain:

- add block
- remove block
- reorder block
- change NAM model asset
- change cab IR asset

Parameter edits apply immediately when the block supports live parameter updates. If a parameter cannot be updated safely in place, the engine may rebuild the chain.

For this phase, the engine only needs real DSP implementations for:

- `nam`
- `cab`
- global input gain
- global output gain
- system master output volume
- global safety limit

Unknown block types are preserved but bypassed:

- The UI shows the block as unsupported.
- The engine skips the block.
- Saving the preset does not delete the unsupported block.

Missing assets are preserved but bypassed:

- The UI shows the block as missing.
- The engine skips the block.
- Saving the preset does not delete or rewrite the asset path unless the user changes it.

## Overload And Bypass

The runtime reports measured audio health from the real callback:

- CPU usage or callback budget percentage.
- Over-budget callback count.
- Xrun count where available.
- Safety limiter activity where available.

If repeated over-budget callbacks or xruns indicate overload, the pedal latches into effects bypass:

```text
input -> dry bypass -> output
```

The UI shows:

```text
OVERLOAD - EFFECTS BYPASSED
```

The latch stays active until the user manually re-enables effects or changes preset. The pedal should not auto-reenable effects, because that can flap during performance.

Mute is reserved for unsafe states:

- audio device failure
- output failure
- unrecoverable engine failure
- unsafe runaway output

## Preset Mode

Preset mode is the main performance screen.

It shows:

- Bank name.
- Four large preset blocks in a 2x2 grid.
- Active preset.
- Dirty state if the active working preset is edited.
- Master output volume.
- CPU or callback budget usage.
- Xrun/overload state.
- Effects bypass state.

Footswitch behavior:

- Switch 1 selects preset 1 in the current bank.
- Switch 2 selects preset 2 in the current bank.
- Switch 3 selects preset 3 in the current bank.
- Switch 4 selects preset 4 in the current bank.
- Switch 1 + 2 changes bank down.
- Switch 3 + 4 changes bank up.

Changing preset replaces the working preset with the saved preset from that slot and rebuilds the chain.

The encoder adjusts master output volume in preset mode.

## Edit Mode

Edit mode shows one serial chain wrapped over two rows:

```text
top row:    input -> block -> block -> block
bottom row: block -> block -> block -> output
```

The visual wrapping does not mean split routing. Processing order is still the order of the `blocks` array.

Edit interactions:

- Swipe from the left opens the block drawer.
- The block drawer floats over the chain and can be closed.
- The block drawer groups assets into categories, starting with Amps and Cabs.
- Drag a block from the drawer onto the chain to insert it.
- Drag an existing chain block to reorder it.
- Tap a block to open a bottom parameter drawer.
- The parameter drawer floats over the chain, can be closed, and is hidden when no block is selected.
- Parameter changes update the working preset and live audio immediately.
- The encoder continues to adjust master output volume.
- Save persists the working preset.
- Discard reloads the saved preset.

The first block drawer only needs categories for:

- Neural Amp Models
- Cabs

Later drawers can add dynamics, modulation, delay, reverb, and utility blocks.

## HTML Mockups

After the data model and engine contract are accepted, a desktop HTML mockup can explore the UI before LVGL implementation.

The mockup should use the same preset JSON shape from this spec. It should focus on:

- Preset mode layout.
- Two-row serial chain editor.
- Left block drawer.
- Parameter drawer.
- Dirty/save/discard states.
- Overload/effects-bypassed state.

The mockup is for interaction and layout validation only. It should not become the production UI runtime.

## Tests

The first implementation plan should include small checks for:

- Preset JSON load and save round trip.
- Block reorder preserves ids, enabled state, assets, and params.
- Working preset dirty state changes after edit.
- Save clears dirty state.
- Discard restores saved state.
- Missing assets mark blocks unavailable without deleting them.
- Unsupported block types are skipped without deleting them.
- Overload event latches effects bypass until manually cleared or preset changes.
- Encoder master volume changes do not dirty the active preset.

## Out Of Scope

- Parallel routing.
- Split chains.
- Stereo dual NAM/cab chains.
- Full DSP effects beyond NAM and cab.
- LVGL rendering implementation.
- Touch gesture tuning.
- Encoder acceleration details.
- Asset catalog database.
- Preset cloud sync or sharing.
