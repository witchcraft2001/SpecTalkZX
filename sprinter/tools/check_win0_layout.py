#!/usr/bin/env python3
"""
check_win0_layout.py — validate the WIN0+WIN1+WIN2 payload layout of a linked
win0 EXE (see the port plan's "SprinTalk: переход на трёхоконную раскладку").

Checked against the PAYLOAD link (before win0_exe.py packs it together with
the stage-1 loader): code+rodata span WIN0 (0x0180..0x3FFF) and optionally
WIN1 (0x4000..0x7FFF); _WINRT (RST trampolines, lib/win0/win0_rt.s), _HIGH
(the UNET call dispatcher, unetcore.s -- only present when NET_BACKEND=unet)
and _DATA all live in the private WIN2 page, in that order, ending with the
stack below 0xBF00 (crt0_win0.s's fixed SP). This guard fails the build if
any invariant breaks, instead of silently producing a corrupt EXE.

Usage: check_win0_layout.py <payload.ihx> <payload.noi>
                            [--win0-base 0x0180] [--win1-end 0x8000]
                            [--win2-base 0x8000] [--win2-top 0xBF00]
                            [--stack-min 0x300]

Reads sdld's NoICE symbol file (.noi, produced by the `-j` linker flag our
own .lk scripts already pass), NOT the columnar .map: the .map's symbol-name
column is a fixed width and silently truncates/collides longer area names
(e.g. `s__INITIALIZER` and `s__INITIALIZED` both print as `s__INITIA`), which
would make a .map-based reader misparse the exact symbols this script depends
on. The .noi file has full names, one `DEF <name> <hex>` per line.
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


def noi_syms(path):
    """Return {symbol: value} for the l__/s__ area symbols in an sdld .noi file."""
    syms = {}
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) == 3 and p[0] == 'DEF' and (p[1].startswith('l__') or p[1].startswith('s__')):
                try:
                    syms[p[1]] = int(p[2], 16)
                except ValueError:
                    pass
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ihx')
    ap.add_argument('noi')
    ap.add_argument('--win0-base', type=lambda x: int(x, 0), default=0x0180)
    ap.add_argument('--win1-end', type=lambda x: int(x, 0), default=0x8000)
    ap.add_argument('--win2-base', type=lambda x: int(x, 0), default=0x8000)
    ap.add_argument('--win2-top', type=lambda x: int(x, 0), default=0xBF00)
    ap.add_argument('--stack-min', type=lambda x: int(x, 0), default=0x300)
    a = ap.parse_args()

    lo, hi = ihx_bounds(a.ihx)
    s = noi_syms(a.noi)
    errs = []

    if lo is None:
        sys.exit("LAYOUT ERROR: empty image")

    scode = s.get('s__CODE', 0)
    # Code+rodata areas, in link order: CODE/HOME/INITIALIZER/GSINIT/GSFINAL.
    code_end = max((s.get(f's__{n}', 0) + s.get(f'l__{n}', 0)
                    for n in ('CODE', 'HOME', 'INITIALIZER', 'GSINIT', 'GSFINAL')),
                   default=0)
    if scode < a.win0_base:
        errs.append(f"_CODE 0x{scode:04X} starts below WIN0_CODE 0x{a.win0_base:04X}")
    if code_end > a.win1_end:
        errs.append(f"code ends 0x{code_end:04X}, past WIN1 end 0x{a.win1_end:04X}: "
                     f"code+rodata may only span WIN0+WIN1")

    swinrt = s.get('s__WINRT')
    winrt_top = swinrt + s.get('l__WINRT', 0) if swinrt is not None else a.win2_base
    if swinrt is None:
        errs.append("no _WINRT area: the win0 RST trampolines (win0_rt.s) were not linked")
    elif swinrt != a.win2_base:
        errs.append(f"_WINRT 0x{swinrt:04X} must start exactly at WIN2 base 0x{a.win2_base:04X}")

    shigh = s.get('s__HIGH')
    if shigh is not None:
        hlen = s.get('l__HIGH', 0)
        high_top = shigh + hlen
        if shigh < winrt_top:
            errs.append(f"_HIGH 0x{shigh:04X} overlaps _WINRT (ends 0x{winrt_top:04X})")
    else:
        high_top = winrt_top

    sdata = s.get('s__DATA', 0)
    dlen = s.get('l__DATA', 0) + s.get('l__INITIALIZED', 0) + s.get('l__BSS', 0)
    dtop = sdata + dlen
    if sdata < high_top:
        errs.append(f"_DATA 0x{sdata:04X} overlaps _WINRT/_HIGH (ends 0x{high_top:04X})")
    if sdata < a.win2_base or dtop > a.win2_top:
        errs.append(f"_DATA 0x{sdata:04X}..0x{dtop:04X} is outside WIN2's private range "
                     f"(0x{a.win2_base:04X}..0x{a.win2_top:04X})")

    # win0_exe.py builds the WIN2 blob as one contiguous run from 0x8000 to the
    # image's highest occupied byte, padding the gaps -- so any image content
    # that lands at or above _DATA would both bloat the EXE and overwrite
    # freshly-zeroed globals at load time.
    if hi >= a.win2_top:
        errs.append(f"image ends 0x{hi:04X}, at/above the stack top 0x{a.win2_top:04X}")
    elif sdata and hi >= sdata:
        errs.append(f"image ends 0x{hi:04X}, inside _DATA (starts 0x{sdata:04X}): "
                     f"win0_exe.py would pack initialized bytes over the zeroed globals")

    stack_room = a.win2_top - 1 - dtop
    if stack_room < a.stack_min:
        errs.append(f"only 0x{max(stack_room, 0):X} bytes for stack "
                     f"(need >= 0x{a.stack_min:X}); code/data grew too large for win0's ~47K budget")

    print(f"  layout: image 0x{lo:04X}..0x{hi:04X} ({hi - lo + 1} B)  "
          f"code 0x{scode:04X}..0x{code_end:04X}")
    if swinrt is not None:
        high_str = f"  high 0x{shigh:04X}..0x{high_top:04X}" if shigh is not None else ""
        print(f"  winrt 0x{swinrt:04X}..0x{winrt_top:04X}{high_str}  "
              f"data 0x{sdata:04X}..0x{dtop:04X} ({dlen} B)")
    print(f"  stack 0x{a.win2_top-1:04X}->0x{dtop:04X} ({stack_room} B)  "
          f"code headroom to 0x{a.win1_end:04X}: {a.win1_end - code_end} B")

    if errs:
        for e in errs:
            print(f"  LAYOUT ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
