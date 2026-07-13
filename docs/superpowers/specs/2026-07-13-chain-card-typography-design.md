# Chain card typography

## Goal

Make each signal-chain effect card easier to scan by displaying its category
and effect name without repeating the short internal block type.

## Card content

- The category appears at the top-left in uppercase, light grey Open Sans 14.
- The effect asset name remains the sole main title, centred in Open Sans 22.
- The internal short type (`mod`, `nam`, `reverb`, and similar) is not shown.
- The existing acid-green dot remains the selected-card affordance; category
  colour does not change with selection.

## Scope

Only the signal-chain cards change. Parameter-drawer titles, drag ghosts, and
block-drawer entries retain their existing labels.

## Verification

The LVGL smoke test will assert that the rendered category is uppercase and
that the short internal type is absent, while the asset name remains present.
