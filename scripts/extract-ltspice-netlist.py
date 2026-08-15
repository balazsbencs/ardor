#!/usr/bin/env python3
"""Extract a netlist from an LTspice .asc by resolving the wire graph.

Pin offsets below were verified against known-good coincidences in the files
this has been run on: every pin of every symbol must land on a wire endpoint,
and the script asserts that. It also asserts the other direction, that every
net a wire reaches carries at least one pin or a label, which is what catches
an offset that happens to land on some unrelated wire.

Two pin roles cannot be read off the geometry and are fixed here by function:

  diode   The near pin is the anode. A reverse-protection diode across the
          supply only makes sense one way round, and that settles it.

  LM308   The two left-hand pins are the inputs. The feedback network has to
          reach the inverting one or the stage would latch rather than
          amplify, and that settles which is which.
"""
import re
import sys
from collections import defaultdict

PINS = {
    'res':     [(16, 16), (16, 96)],
    'cap':     [(16, 0), (16, 64)],
    'ind':     [(16, 16), (16, 96)],
    'voltage': [(0, 16), (0, 96)],
    'zener':   [(16, 0), (16, 64)],
    'diode':   [(16, 0), (16, 64)],
    'npn':     [(0, 48), (64, 0), (64, 96)],  # B, C, E
    'njf':     [(48, 0), (0, 64), (48, 96)],  # D, G, S
    # Seven pins, because the LM308 brings its compensation network out.
    'LM308':   [(-32, 48), (-32, 80), (32, 64), (16, 32), (0, 96), (0, 32), (-16, 32)],
}
PINNAMES = {
    'npn': ['B', 'C', 'E'],
    'njf': ['D', 'G', 'S'],
    'diode': ['A', 'K'],
    'zener': ['A', 'K'],
    'LM308': ['IN-', 'IN+', 'OUT', 'V+', 'V-', 'COMP1', 'COMP2'],
}


def xform(rot, x, y, dx, dy):
    if rot == 'R0':    return (x + dx, y + dy)
    if rot == 'R90':   return (x - dy, y + dx)
    if rot == 'R180':  return (x - dx, y - dy)
    if rot == 'R270':  return (x + dy, y - dx)
    if rot == 'M0':    return (x - dx, y + dy)
    if rot == 'M90':   return (x + dy, y + dx)
    if rot == 'M180':  return (x + dx, y - dy)
    if rot == 'M270':  return (x - dy, y - dx)
    raise ValueError(rot)


class UF:
    def __init__(self):
        self.p = {}

    def find(self, a):
        self.p.setdefault(a, a)
        while self.p[a] != a:
            self.p[a] = self.p[self.p[a]]
            a = self.p[a]
        return a

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[ra] = rb


def main(path):
    lines = open(path, encoding='latin-1').read().splitlines()
    uf = UF()
    endpoints = set()
    flags = {}
    symbols = []
    cur = None

    for line in lines:
        t = line.split()
        if not t:
            continue
        if t[0] == 'WIRE':
            a = (int(t[1]), int(t[2]))
            b = (int(t[3]), int(t[4]))
            uf.union(a, b)
            endpoints.add(a)
            endpoints.add(b)
        elif t[0] == 'FLAG':
            flags[(int(t[1]), int(t[2]))] = t[3]
        elif t[0] == 'SYMBOL':
            cur = {'type': t[1].split('\\')[-1], 'x': int(t[2]),
                   'y': int(t[3]), 'rot': t[4], 'name': None, 'value': None}
            symbols.append(cur)
        elif t[0] == 'SYMATTR' and cur is not None:
            if t[1] == 'InstName':
                cur['name'] = t[2]
            elif t[1] == 'Value':
                cur['value'] = ' '.join(t[2:])

    # Name each electrical node.
    names = {}
    for pt, nm in flags.items():
        names[uf.find(pt)] = nm
    counter = [0]

    def node_of(pt):
        r = uf.find(pt)
        if r not in names:
            counter[0] += 1
            names[r] = 'n%d' % counter[0]
        return names[r]

    print('%-6s %-9s %-22s %s' % ('DEV', 'TYPE', 'NODES', 'VALUE'))
    print('-' * 72)
    unresolved = []
    claimed = set()
    for s in symbols:
        offs = PINS.get(s['type'])
        if offs is None:
            print('!! unknown symbol type: %s (%s)' % (s['type'], s['name']))
            continue
        pts = [xform(s['rot'], s['x'], s['y'], dx, dy) for dx, dy in offs]
        for p in pts:
            if p not in endpoints and p not in flags:
                unresolved.append((s['name'], p))
            claimed.add(p)
        labels = PINNAMES.get(s['type'], [str(i) for i in range(len(pts))])
        nodes = ' '.join('%s=%s' % (lab, node_of(p)) for lab, p in zip(labels, pts))
        print('%-6s %-9s %-22s %s' % (s['name'], s['type'], nodes, s['value']))

    if unresolved:
        print('\n!! PINS NOT ON A WIRE ENDPOINT (offsets wrong?):')
        for nm, p in unresolved:
            print('   %s at %s' % (nm, p))
        return 1

    # The other direction: a net that no pin and no label reaches means an
    # offset landed somewhere plausible but wrong.
    live = {uf.find(p) for p in claimed} | {uf.find(p) for p in flags}
    orphans = sorted(r for r in {uf.find(p) for p in endpoints} if r not in live)
    if orphans:
        print('\n!! NETS THAT REACH NO COMPONENT: %s' % (orphans,))
        return 1

    print('\nAll pins landed on wire endpoints, and every net carries a pin.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
