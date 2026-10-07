#!/usr/bin/env python3
"""Compare the header and planning-record multiset of retained FFTW wisdom."""
from collections import Counter
from pathlib import Path
import hashlib
import re
import sys

root = Path(sys.argv[1])

def read(path):
    tokens = iter(re.findall(r"\(|\)|[^\s()]+", path.read_text()))
    def value(token):
        if token != "(":
            assert token != ")"
            return token
        result = []
        for token in tokens:
            if token == ")":
                return tuple(result)
            result.append(value(token))
        raise ValueError("unterminated wisdom expression")
    parsed = value(next(tokens))
    assert next(tokens, None) is None
    header = tuple(item for item in parsed if not isinstance(item, tuple))
    records = Counter(item for item in parsed if isinstance(item, tuple))
    return header, records

shared = root / "shared.wisdom"
header, records = read(shared)
print("shared_wisdom_sha256=" + hashlib.sha256(shared.read_bytes()).hexdigest())
print("records=" + str(sum(records.values())))
assert sum(records.values()) == 57
exports = sorted(root.glob("shared.wisdom.*.after"))
assert len(exports) == 4
for path in exports:
    after_header, after_records = read(path)
    added = sum((after_records - records).values())
    removed = sum((records - after_records).values())
    print(f"{path.name}: same_header={after_header == header} added={added} removed={removed}")
    assert after_header == header and added == removed == 0
