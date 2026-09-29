"""Export and verify the two-board SMT-only JLCPCB BOM, CPL and sourcing audit.

First refresh assembly/kicad-smt-positions.csv with KiCad's CSV/mm position export.
Uses the checked public inventory snapshot; does not reserve stock or place orders.
"""
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

import pcbnew as pcb

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assembly"


def write_csv(name, fields, rows):
    with (OUT / name).open("w") as f:
        writer = csv.DictWriter(f, fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def magnitude(text):
    text = text.replace("Ω", "").replace("F", "").replace("V", "").strip()
    if "R" in text:
        return float(text.replace("R", "."))
    match = re.fullmatch(r"([\d.]+)([pnumkM]?)", text)
    assert match, text
    return float(match[1]) * {"": 1, "p": 1e-12, "n": 1e-9, "u": 1e-6, "m": 1e-3, "k": 1e3, "M": 1e6}[match[2]]


def main():
    config = json.loads((OUT / "parts.json").read_text())
    quantity = config["board_quantity"]
    parts = {ref: part for part in config["parts"] for ref in part["references"]}
    stock = {p["componentCode"]: p for p in json.loads((OUT / "inventory-snapshot.json").read_text())["parts"]}
    with (ROOT / "BOM.csv").open() as f:
        master = {r["Reference"]: r for r in csv.DictReader(f)}
    with (OUT / "kicad-smt-positions.csv").open() as f:
        positions = {r["Ref"]: r for r in csv.DictReader(f)}
    board = pcb.LoadBoard(str(ROOT / "Ardor_IO.kicad_pcb"))
    footprints = {f.GetReference(): f for f in board.GetFootprints()}
    schematic = {c.attrib["ref"]: c for c in ET.parse(ROOT / "review/fresh-netlist.xml").findall(".//components/comp")}
    # Completeness is derived from actual board attributes, not just the selection file.
    smt = {ref for ref, f in footprints.items() if f.GetAttributes() & pcb.FP_SMD
           and not ref.startswith("TP") and not f.IsDNP()}
    assert set(parts) == smt == set(positions)
    assert len(smt) == 76
    cpl, polarity = [], []
    for ref in sorted(smt):
        part, f, row, pos, comp = parts[ref], footprints[ref], master[ref], positions[ref], schematic[ref]
        assert not f.IsFlipped() and pos["Side"] == "top"
        assert row["JLCPCB Part #"] == f.GetFieldByName("JLCPCB Part #").GetText() == part["jlcpcb_part"]
        assert comp.findtext("fields/field[@name='JLCPCB Part #']") == part["jlcpcb_part"]
        assert row["MPN"] == f.GetFieldByName("MPN").GetText() == comp.findtext("fields/field[@name='MPN']") == part["bom_mpn"]
        assert row["Datasheet"] == comp.findtext("datasheet") == f.GetFieldByName("Datasheet").GetText() == part["datasheet"]
        assert row["Value"] == comp.findtext("value") == f.GetValue() == pos["Val"]
        assert row["Footprint"] == comp.findtext("footprint") == part["footprint"]
        assert str(f.GetFPID().GetLibNickname()) + ":" + str(f.GetFPID().GetLibItemName()) == part["footprint"]
        attrs = part["catalog_attributes"]
        if ref.startswith("R"):
            nominal = row["Value"].split(" / ")[0]
            assert abs(magnitude(nominal) - magnitude(attrs["Resistance"])) < 1e-9
            if "0.1%" in row["Value"]:
                assert attrs["Tolerance"] == "±0.1%"
            elif nominal != "0R":
                assert float(attrs["Tolerance"].strip("±%")) <= 1
        elif ref.startswith("C"):
            nominal, rating = row["Value"].split(" / ")
            assert abs(magnitude(nominal) - magnitude(attrs["Capacitance"])) < 1e-12
            assert magnitude(attrs["Voltage Rating"]) >= magnitude(rating.split()[0])
            if "X7R" in rating or "C0G" in rating:
                assert attrs["Temperature Coefficient"] == rating.split()[1]
        standard_package = next((s for s in ["0603", "0805", "1206"]
                                 if "_" + s + "_" in part["footprint"]), None)
        if standard_package:
            assert part["catalog_package"] == standard_package
        # KiCad's default export origin is absolute X, inverted Y; keep that origin.
        assert abs(float(pos["PosX"]) - pcb.ToMM(f.GetPosition().x)) < 1e-5
        assert abs(float(pos["PosY"]) + pcb.ToMM(f.GetPosition().y)) < 1e-5
        assert abs((float(pos["Rot"]) - f.GetOrientationDegrees() + 180) % 360 - 180) < 1e-5
        cpl.append({"Designator": ref, "Mid X": pos["PosX"] + "mm", "Mid Y": pos["PosY"] + "mm",
                    "Layer": "Top", "Rotation": f"{float(pos['Rot']) % 360:.6f}"})
        if ref in {"C501", "D201", "D502", "D303", "D304", "D305", "D306", "Q501", "U301", "U401", "U402", "U501", "U601"}:
            meaning = "positive terminal" if ref == "C501" else "cathode" if ref.startswith("D") else "gate" if ref == "Q501" else "pin 1"
            pin = next(p for p in f.Pads() if p.GetNumber() == "1")
            polarity.append({"Designator": ref, "MPN": part["mpn"], "Marker": meaning,
                             "Pin": "1", "Pin X mm": f"{pcb.ToMM(pin.GetPosition().x):.6f}",
                             "Pin Y mm": f"{-pcb.ToMM(pin.GetPosition().y):.6f}",
                             "Net": pin.GetNetname(), "KiCad rotation": f.GetOrientationDegrees()})
    bom, inventory = [], []
    for part in config["parts"]:
        code, refs = part["jlcpcb_part"], part["references"]
        s = stock[code]
        assert s["componentModelEn"] == part["mpn"]
        assert s["componentSpecificationEn"] == part["catalog_package"]
        assert s["assemblyMode"] == "smtWeld"
        fitted = len(refs) * quantity
        # Published per-part minimum and loss quantities, not a checkout quotation.
        required = max(fitted + (s["lossNumber"] or 0), s["leastPatchNumber"] or 0)
        assert s["canPresaleNumber"] >= required, (code, required)
        bom.append({"Comment": part["mpn"], "Designator": ",".join(refs),
                    "Footprint": part["catalog_package"], "LCSC Part #": code})
        inventory.append({"JLCPCB Part #": code, "Manufacturer": part["manufacturer"], "MPN": part["mpn"],
                          "Designators": ",".join(refs), "Package": part["catalog_package"],
                          "Per board": len(refs), "Fitted on 2 boards": fitted,
                          "Published loss allowance": s["lossNumber"], "Published minimum placements": s["leastPatchNumber"],
                          "Estimated charged quantity": required, "Available order quantity": s["canPresaleNumber"],
                          "Library": "Basic" if s["componentLibraryType"] == "base" else "Extended",
                          "Fixture required": s["fixtureFlag"], "Checked UTC": s["checked_utc"], "Source": s["url"]})
    manual = []
    for ref, row in master.items():
        if ref in smt or ref.startswith("TP"):
            continue
        manual.append({"Designator": ref, "Value": row["Value"], "MPN": row["MPN"],
                       "Footprint": row["Footprint"], "Quantity for 2 boards": quantity,
                       "Assembly": "Hand solder" if row["Footprint"] else "Panel / wire by hand"})
    manual.append({"Designator": "JP301 shunts", "Value": "2.54 mm jumper shunt", "MPN": "",
                   "Footprint": "Two shunts per JP301", "Quantity for 2 boards": 2 * quantity, "Assembly": "Fit by hand"})
    write_csv("JLCPCB_BOM.csv", ["Comment", "Designator", "Footprint", "LCSC Part #"], bom)
    write_csv("JLCPCB_CPL.csv", ["Designator", "Mid X", "Mid Y", "Layer", "Rotation"], cpl)
    write_csv("inventory.csv", list(inventory[0]), inventory)
    write_csv("hand-assembly.csv", list(manual[0]), manual)
    write_csv("polarity-reference.csv", list(polarity[0]), polarity)
    for name in ["violations", "unconnected_items", "schematic_parity"]:
        assert not json.loads((ROOT / "routing/drc.json").read_text())[name]
    assert not any(sheet["violations"] for sheet in json.loads((ROOT / "review/fresh-erc.json").read_text())["sheets"])
    report = {"board_quantity": quantity, "smt_placements_per_board": len(smt), "unique_smt_parts": len(bom),
              "basic_parts": sum(p["Library"] == "Basic" for p in inventory),
              "extended_parts": sum(p["Library"] == "Extended" for p in inventory),
              "all_parts_stocked_above_published_minimum_and_loss_allowance": True,
              "bom_cpl_schematic_board_reference_sets_match": True,
              "all_mpn_footprint_datasheet_and_jlc_fields_match": True,
              "passive_values_voltage_ratings_dielectrics_and_resistor_tolerances_checked": True,
              "all_cpl_coordinates_and_rotations_match_kicad": True,
              "coordinate_convention": "KiCad absolute origin: X right, Y up; no auxiliary-origin shift",
              "jlcpcb_preview_rotations_verified": False,
              "stock_reserved": False, "order_placed": False,
              "erc_violations": 0, "drc_violations": 0, "unconnected_items": 0, "schematic_parity_issues": 0,
              "board_sha256": hashlib.sha256((ROOT / "Ardor_IO.kicad_pcb").read_bytes()).hexdigest()}
    (OUT / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
