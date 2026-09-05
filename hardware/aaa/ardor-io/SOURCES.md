# Primary design references

Reviewed 5 September 2026. URLs may publish newer revisions later. The circuit design and calculations are specific to this project; the links below are the underlying interface and device references.

- [Raspberry Pi Codec Zero product page](https://www.raspberrypi.com/products/codec-zero/) — AUX stereo interface and board capabilities.
- [Raspberry Pi audio documentation](https://www.raspberrypi.com/documentation/accessories/audio.html) — Codec Zero operation and GPIO usage.
- [MIDI electrical specification update](https://midi.org/wp-content/uploads/wpforo/default_attachments/1709416667-ca33-MIDI-10-Electrical-Specification-Update.pdf) — DIN current-loop interface, 3.3/5 V interoperability and RF provisions.
- [onsemi H11L1M family data sheet](https://www.onsemi.com/pdf/datasheet/h11l3m-d.pdf) — optocoupler pinout, open-collector output and current threshold.
- [TI ADS1115 data sheet](https://www.ti.com/lit/ds/symlink/ads1115.pdf) — converter, address selection, input limits and PGA configuration.
- [TI OPA2320 data sheet](https://www.ti.com/lit/ds/symlink/opa2320.pdf) — 5 V RRIO buffers, SOIC pinout and unity-gain operation.
- [TI TPA6132A2 data sheet](https://www.ti.com/lit/ds/symlink/tpa6132a2.pdf) — single-ended input configuration, gain straps, charge-pump connections and output limits.
- [Nexperia PESD5V0S1BA data sheet](https://assets.nexperia.com/documents/data-sheet/PESD5V0S1BA.pdf) — bidirectional connector ESD suppression.
- [Nexperia PESD24VL1BA data sheet](https://assets.nexperia.com/documents/data-sheet/PESD24VL1BA.pdf) — MIDI common-mode protection, leakage and standoff voltage.
- [Nexperia BAT54H data sheet](https://assets.nexperia.com/documents/data-sheet/BAT54H.pdf) — secondary ADC clamp diodes and SOD123F package.
- [Omron G5V-1 data sheet](https://components.omron.com/us-en/system/files/2023-01/datasheet_pdf/K048-E1.pdf) — relay coil, common/NC/NO contacts and footprint.
- [WIMA MKS2 current product data](https://www.wima.de/en/our-product-range/metallized-capacitors/mks-2/) — 2.2 µF / 63 V film capacitor body dimensions and part-number construction.

KiCad's standard symbols were copied into a project-local library. The OPA2320 uses the verified standard dual-op-amp SOIC pin mapping. In the TPA6132A2 symbol, HPVDD/HPVSS were changed from `power_in` to `power_out` to accurately model the internal supply generators for ERC; their physical pin numbers are unchanged.
