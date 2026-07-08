#include "ui/UiModel.h"

#include <iostream>

namespace {

int require(bool ok, const char* message)
{
  if (!ok) {
    std::cerr << message << "\n";
    return 1;
  }
  return 0;
}

} // namespace

int main()
{
  auto state = ardor::makeDemoUiState();
  if (require(state.bank.presets.size() == 4, "expected four presets")) return 1;
  if (require(state.bank.name == "Bank 000 - Core Sounds", "expected demo bank name")) return 1;
  if (require(state.activePreset == 0, "expected first preset active")) return 1;
  if (require(state.mode == ardor::UiMode::Preset, "expected preset mode")) return 1;
  if (require(state.masterVolume == 82, "expected demo master volume")) return 1;

  ardor::selectPreset(state, 2);
  if (require(state.activePreset == 2, "preset selection failed")) return 1;
  if (require(state.mode == ardor::UiMode::Preset, "preset selection should return to preset mode")) return 1;
  if (require(!state.blockDrawerOpen, "preset selection should close block drawer")) return 1;
  if (require(!state.paramDrawerOpen, "preset selection should close parameter drawer")) return 1;

  ardor::selectPreset(state, 99);
  if (require(state.activePreset == 2, "invalid preset selection should be ignored")) return 1;

  ardor::enterEditMode(state);
  if (require(state.mode == ardor::UiMode::Edit, "edit mode should be active")) return 1;
  ardor::openBlockDrawer(state);
  if (require(state.blockDrawerOpen, "block drawer should open")) return 1;
  ardor::setCategoryFilter(state, "modulation");
  if (require(state.categoryFilter == "modulation", "category filter failed")) return 1;
  ardor::closeBlockDrawer(state);
  if (require(!state.blockDrawerOpen, "block drawer close failed")) return 1;
  ardor::setCategoryFilter(state, "bogus");
  if (require(state.categoryFilter == "all", "bad filter should fall back to all")) return 1;

  ardor::selectBlock(state, 0);
  if (require(state.paramDrawerOpen, "block selection should open parameter drawer")) return 1;
  if (require(state.selectedBlock == 0, "selected block index failed")) return 1;
  ardor::closeParamDrawer(state);
  if (require(!state.paramDrawerOpen, "parameter drawer close failed")) return 1;

  ardor::enterPresetMode(state);
  if (require(state.mode == ardor::UiMode::Preset, "preset mode should be active")) return 1;
  if (require(!state.blockDrawerOpen && !state.paramDrawerOpen, "preset mode should close drawers")) return 1;

  return 0;
}
