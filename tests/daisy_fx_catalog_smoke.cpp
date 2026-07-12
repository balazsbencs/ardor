#include "daisyfx/DaisyFxCatalog.h"

#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main()
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("mod", "vintage_trem");
  require(descriptor != nullptr, "find vintage trem");
  require(descriptor->kind == ardor::DaisyFxKind::Mod, "vintage trem kind");
  require(descriptor->blockType == "mod", "vintage trem block type");
  require(descriptor->mode == "vintage_trem", "vintage trem mode");
  require(descriptor->name == "Vintage Trem", "vintage trem name");
  require(descriptor->params.size() == 7, "vintage trem param count");

  const auto defaults = ardor::defaultDaisyFxParams(*descriptor);
  require(defaults.value("mode", "") == "vintage_trem", "default mode");
  for (const auto* key : {"speed", "depth", "mix", "tone", "p1", "p2", "level"}) {
    require(defaults.contains(key), std::string{"default contains "} + key);
    require(defaults.at(key).is_number(), std::string{"default numeric "} + key);
  }

  require(ardor::findDaisyFxDescriptor("mod", "bogus") == nullptr, "reject unknown mode");
  require(ardor::findDaisyFxDescriptor("delay", "vintage_trem") == nullptr, "reject wrong block type");

  const auto* delay = ardor::findDaisyFxDescriptor("delay", "digital");
  require(delay != nullptr, "find digital delay");
  require(delay->kind == ardor::DaisyFxKind::Delay, "digital delay kind");
  require(ardor::defaultDaisyFxParams(*delay).value("mode", "") == "digital", "digital delay default mode");
  require(ardor::defaultDaisyFxParams(*delay).contains("repeats"), "digital delay defaults");

  const auto* reverb = ardor::findDaisyFxDescriptor("reverb", "room");
  require(reverb != nullptr, "find room reverb");
  require(reverb->kind == ardor::DaisyFxKind::Reverb, "room reverb kind");
  require(ardor::defaultDaisyFxParams(*reverb).value("mode", "") == "room", "room reverb default mode");
  require(ardor::defaultDaisyFxParams(*reverb).contains("decay"), "room reverb defaults");
}
