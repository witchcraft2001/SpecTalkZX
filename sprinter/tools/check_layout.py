#!/usr/bin/env python3
"""
check_layout.py — validate the flat WIN1+WIN2 memory layout of a linked EXE.

For crt0_flat the program is ONE loaded image spanning WIN1 (0x4200..0x7FFF) and
WIN2 (0x8000..0xBFFF). DSS only maps the WIN2 page if the image actually reaches
past 0x8000, and data/stack live high in WIN2. This guard fails the build if any
invariant breaks, instead of silently producing a corrupt EXE.

Usage: check_layout.py <file.ihx> <file.map> [--win2-base 0x8000]
                       [--win2-end 0xC000] [--stack-min 0x300]
"""
import sys
import argparse


def ihx_bounds(path):
    lo, hi = None, None
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line[0] != ':':
                continue
            raw = bytes.fromhex(line[1:])
            n, addr, rtype = raw[0], (raw[1] << 8) | raw[2], raw[3]
            if rtype != 0 or n == 0:
                continue
            lo = addr if lo is None else min(lo, addr)
            hi = (addr + n - 1) if hi is None else max(hi, addr + n - 1)
    return lo, hi


def map_syms(path):
    """Return {symbol: value} for the l__/s__ area symbols in an sdld .map."""
    syms = {}
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) >= 2 and (p[1].startswith('l__') or p[1].startswith('s__')):
                try:
                    syms[p[1]] = int(p[0], 16)
                except ValueError:
                    pass
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ihx')
    ap.add_argument('map')
    ap.add_argument('--win2-base', type=lambda x: int(x, 0), default=0x8000)
    ap.add_argument('--win2-end', type=lambda x: int(x, 0), default=0xC000)
    ap.add_argument('--stack-min', type=lambda x: int(x, 0), default=0x300)
    a = ap.parse_args()

    lo, hi = ihx_bounds(a.ihx)
    s = map_syms(a.map)
    sdata = s.get('s__DATA', 0)
    dlen = s.get('l__DATA', 0) + s.get('l__INITIALIZED', 0) + s.get('l__BSS', 0)
    dtop = sdata + dlen
    stack_room = a.win2_end - 1 - dtop          # 0xBFFF down to data top
    errs = []

    if hi is None:
        errs.append("empty image")
    elif hi < a.win2_base:
        errs.append(f"image ends 0x{hi:04X} < WIN2 base 0x{a.win2_base:04X}: "
                    f"DSS will NOT map WIN2, so data/stack are unmapped. "
                    f"Pad the image past 0x{a.win2_base:04X}.")
    if sdata <= (hi or 0):
        errs.append(f"_DATA 0x{sdata:04X} overlaps code image (ends 0x{hi:04X})")
    if dtop > a.win2_end:
        errs.append(f"data top 0x{dtop:04X} overflows WIN2 end 0x{a.win2_end:04X}")
    if stack_room < a.stack_min:
        errs.append(f"only 0x{max(stack_room,0):X} bytes for stack "
                    f"(need >= 0x{a.stack_min:X}); lower DATA_LOC")

    code_ceiling = sdata
    print(f"  layout: image 0x{lo:04X}..0x{hi:04X} "
          f"({hi - lo + 1} B)  data 0x{sdata:04X}..0x{dtop:04X} ({dlen} B)  "
          f"stack 0x{a.win2_end-1:04X}->0x{dtop:04X} ({stack_room} B)")
    print(f"  code headroom to 0x{code_ceiling:04X}: "
          f"{code_ceiling - hi} B")

    if errs:
        for e in errs:
            print(f"  LAYOUT ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
