#include "wah/WahNetlist.h"

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
  const auto& netlist = ardor::gcb95Netlist();
  require(ardor::wahNetlistValid(netlist), "the shipped GCB-95 netlist should validate");

  // A netlist with a non-positive component value is not a circuit.
  ardor::WahNetlist broken = netlist;
  broken.inductorHenries = 0.0;
  require(!ardor::wahNetlistValid(broken), "a zero inductor should fail validation");
  broken = netlist;
  broken.potOhms = -1.0;
  require(!ardor::wahNetlistValid(broken), "a negative pot resistance should fail validation");

  // The taper spans the whole pot and never reverses: the treadle must sweep
  // in one direction only, or the wah will sound like it stalls mid-throw.
  const double heel = ardor::wahPotWiperOhms(netlist, 0.0);
  const double toe = ardor::wahPotWiperOhms(netlist, 1.0);
  require(heel >= 0.0 && heel < netlist.potOhms * 0.02,
          "heel position should sit at the bottom of the pot track");
  require(toe > netlist.potOhms * 0.98 && toe <= netlist.potOhms,
          "toe position should reach the top of the pot track");

  double previous = -1.0;
  for (int i = 0; i <= 100; ++i) {
    const double value = ardor::wahPotWiperOhms(netlist, i / 100.0);
    require(value > previous, "wiper resistance should increase monotonically with position");
    previous = value;
  }

  // Audio taper, not linear: the midpoint must sit well below half the track.
  const double middle = ardor::wahPotWiperOhms(netlist, 0.5);
  require(middle < netlist.potOhms * 0.30,
          "an audio-taper pot should be well under 30% of track resistance at midpoint");

  // Out-of-range positions clamp rather than extrapolate.
  require(ardor::wahPotWiperOhms(netlist, -1.0) == heel, "positions below 0 should clamp to heel");
  require(ardor::wahPotWiperOhms(netlist, 2.0) == toe, "positions above 1 should clamp to toe");
  return 0;
}
