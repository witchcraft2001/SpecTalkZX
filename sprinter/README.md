# SpecTalk ZX — Sprinter DSS port

Port of the ZX Spectrum IRC client to the Sprinter computer (DSS), built with the
SDCC 4.5 Sprinter SDK. See `../plan.md` for the full plan and `../CLAUDE.md` for
context. Work proceeds in verifiable stages; this directory is the Sprinter
build, independent of the ZX `src/`/`asm/` tree.

## Build

```sh
make            # -> SPECTALK.EXE (DSS executable, load/entry 0x4100)
make deploy     # build EXE + write a FAT12 floppy to the path 306_sdcc450.sh mounts
make clean
```

Override the SDK path if needed: `make SDK=/path/to/sdcc45-sprinter-sdk`.

## Verify in MAME

`make deploy` writes the floppy to
`<SDK>/build/sprinter.img`, which the MAME launcher already mounts as flop2:

```sh
cd /Users/dmitry/dev/zx/sprinter/mame_images/mame_release_v306_25.05.2025
./306_sdcc450.sh
```

At the DSS prompt, switch to the floppy drive and run `SPECTALK`.

## Stages

1. **HAL smoke test** (current) — 80x32 text mode, full-screen border, live clock,
   non-blocking keyboard echo, clean exit.
2. Terminal HAL + chat layout.
3. net.cfg loader + ISA/UART diagnostics.
4. ESP TCP backend + IRC handshake.
5. IRC engine + UI integration.
6. Feature parity.
7. NE2000 backend + packaging.
