"""Synchronize reviewed JLCPCB selections into schematic, board and master BOM.

Only procurement properties and explicitly selected capacitor voltage labels change.
Requires KiCad 9 Python and sexpdata. Physical footprints and routing are retained.
"""
import csv
import copy
import json
from pathlib import Path

import pcbnew as pcb
import sexpdata as sx

ROOT = Path(__file__).resolve().parents[1]


def kind(v):
    return str(v[0]) if isinstance(v, list) and v else ""


def properties(v):
    return {p[1]: p for p in v if kind(p) == "property"}


def main():
    selection = json.loads((ROOT / "assembly/parts.json").read_text())
    parts = {ref: part for part in selection["parts"] for ref in part["references"]}
    assert len(parts) == sum(len(part["references"]) for part in selection["parts"])
    board = pcb.LoadBoard(str(ROOT / "Ardor_IO.kicad_pcb"))
    footprints = {f.GetReference(): f for f in board.GetFootprints()}
    with (ROOT / "BOM.csv").open() as f:
        reader = csv.DictReader(f)
        fields, rows = reader.fieldnames, list(reader)
    bom = {r["Reference"]: r for r in rows}
    sheets = {path: sx.loads(path.read_text()) for path in ROOT.glob("*.kicad_sch")}
    for ref, part in parts.items():
        for key in ["bom_mpn", "datasheet", "jlcpcb_part"]:
            assert isinstance(part[key], str) and part[key], (ref, key)
        f = footprints[ref]
        assert str(f.GetFPID().GetLibNickname()) + ":" + str(f.GetFPID().GetLibItemName()) == part["footprint"]
        assert bom[ref]["Footprint"] == part["footprint"]
        assert f.GetAttributes() & pcb.FP_SMD
    for sheet in sheets.values():
        for symbol in sheet:
            if kind(symbol) != "symbol":
                continue
            props = properties(symbol)
            ref = props["Reference"][2]
            if ref not in parts:
                continue
            part = parts[ref]
            assert props["Footprint"][2] == part["footprint"]
            for name in ["MPN", "Datasheet"]:
                assert name in props, (ref, name)

    for ref, part in parts.items():
        values = {"MPN": part["bom_mpn"], "Datasheet": part["datasheet"],
                  "JLCPCB Part #": part["jlcpcb_part"]}
        if "schematic_value" in part:
            values["Value"] = part["schematic_value"]
        bom[ref].update(values)
        for name, value in values.items():
            f = footprints[ref]
            field = f.GetFieldByName(name)
            if field is None:
                field = pcb.PCB_FIELD(f, f.GetNextFieldId(), name)
                f.AddField(field)
                field = f.GetFieldByName(name)
            field.SetText(value)
            field.SetVisible(False)
        for sheet in sheets.values():
            for symbol in sheet:
                if kind(symbol) == "symbol":
                    props = properties(symbol)
                    if props["Reference"][2] == ref:
                        for name, value in values.items():
                            if name not in props:
                                props[name] = copy.deepcopy(props["MPN"])
                                props[name][1] = name
                                symbol.append(props[name])
                            props[name][2] = value
    pcb.SaveBoard(str(ROOT / "Ardor_IO.kicad_pcb"), board)
    for path, sheet in sheets.items():
        path.write_text(sx.dumps(sheet) + "\n")
    with (ROOT / "BOM.csv").open("w") as f:
        writer = csv.DictWriter(f, fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    print(f"Synchronized {len(parts)} SMT placements; no footprint or routing edits.")


if __name__ == "__main__":
    main()
