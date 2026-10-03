"""Load the two reviewed width-only plans, without changing the source board."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def plans():
    rails = json.loads((ROOT / 'design/uniform-3v3-routing.json').read_text())
    all_nets = json.loads((ROOT / 'design/trace-width-plan.json').read_text())
    changes = {item['uuid']: {**item, 'after_width_mm': rails['target_width_mm']}
               for item in rails['changes']}
    assert len(changes) == len(rails['changes'])
    for item in all_nets['changes']:
        assert item['uuid'] not in changes, 'Width plans overlap'
        changes[item['uuid']] = item
    exceptions = {item['uuid']: item for item in all_nets['exceptions']}
    assert len(exceptions) == len(all_nets['exceptions'])
    return rails, all_nets, changes, exceptions
