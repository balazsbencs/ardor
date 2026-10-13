# Sources and assumptions

The M1 circuits reuse the reviewed Rev C/full IO building blocks, with local supplies/bias and AC-coupled interfaces added to remove shared-board dependencies. Files under `hardware/aaa/sources` are untouched. Frozen local footprints and symbols make each module project independent of the original board and shared CAD libraries.

Primary electrical references, checked 2 October 2026:

- [TI ADS1115 datasheet](https://www.ti.com/lit/ds/symlink/ads1115.pdf): pin map, 3.3 V I²C operation, address selection, PGA and single-ended channels. Input clamps do not replace operating limits.
- [TI TLV9002 datasheet](https://www.ti.com/lit/ds/symlink/tlv9002.pdf): 5 V unity-buffer operation, rail limits and local decoupling. These economical amplifiers trade noise/bandwidth against the original IO choice.
- [TI TPA6132A2 datasheet](https://www.ti.com/lit/ds/symlink/tpa6132a2.pdf): QFN pin map, local charge pump, exposed-pad ground, enable threshold and gain settings. Both gain pins LOW select −6 dB. Enable must remain within the powered device’s absolute limits.
- [onsemi H11L1M family datasheet](https://www.onsemi.com/download/data-sheet/pdf/h11l3m-d.pdf): Schmitt optocoupler, DIP-6 pin map, 5 V operation and open-collector output. Use the specified H11L1M.
- [Omron G5V-1 datasheet](https://omronfs.omron.com/en_US/ecb/products/pdf/en-g5v_1.pdf): DC5 coil approximately 30 mA / 167 Ω, contact numbering and package. The relay grounds the panel tip when off; it does not route a dry input.

Manufacturing and price references, checked 2 October 2026:

- [JLCPCB assembly pricing](https://jlcpcb.com/help/article/pcb-assembly-price): Economic Extended loading $3.07/type; QFN X-ray approximately $1.64/board. Setup/stencil/joint costs remain separate.
- [JLCPCB PCB capabilities](https://jlcpcb.com/capabilities/pcb-capabilities/): two-layer through-via/drill limits. Ordinary vias are 0.60/0.30 mm; exposed-pad thermal holes are 0.50/0.20 mm, giving 0.15 mm nominal annular rings. No blind vias or specified controlled impedance.
- [JLCPCB assembly capabilities](https://jlcpcb.com/capabilities/pcb-assembly-capabilities): all boards exceed the 10 × 10 mm Economic minimum; smallest component pitch is 0.5 mm. Final manufacturability and price are confirmed by upload review, not inferred from stock alone.

Each `assembly/inventory-snapshot.json` stores public part identity, stock, package attributes, prices, loss/minimum-patch allowances and source URLs. The assembly exporter validates exact MPN/C-code equality and sufficient available stock for two boards. The price model uses `max(2 × placements + lossNumber, leastPatchNumber)` per C-code at the snapshot unit price; `preMinPurchaseNum` is a wholesale purchasing field and is not added as an assembly minimum. The price model is indicative, excludes discounts and unlisted checkout charges, and is not an order reservation. Manual-fit parts and panel sockets are excluded. Do not reuse a dated stock claim as a guarantee of availability.

The projects have no manufacturer placement-preview approval and no measured hardware results. Bench audio/ESD/EMC/thermal/relay-startup performance, cable behavior and enclosure bonding remain commissioning work. Optocoupler isolation here addresses functional MIDI ground loops; no safety isolation rating or mains use is asserted.
