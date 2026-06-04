# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Active work: Sprinter DSS port

This repo is being **ported to the Sprinter computer running DSS** (native 80×32
text mode, SDCC Sprinter SDK build, swappable network backends). The port plan —
scope, target-platform API details, reuse-vs-replace breakdown, the universal
network core design, and the phased milestones — lives in **`plan.md`**. Read it
before doing port work. In short: keep the portable IRC engine and UI/state
model (`irc_handlers.c`, `user_cmds.c`, the channel/window logic), replace the
entire ZX/z88dk platform layer (rendering, keyboard, UART, file I/O, overlays,
BPE, memory map, build) with a Sprinter HAL on the SDCC 4.5 SDK at
`/Users/dmitry/dev/zx/sprinter/sdcc45-sprinter-sdk`.

Hard-won Sprinter platform facts (memory layout/WIN1-only, keyboard codes, CP866,
`dss_clear`/`dss_puts`/scroll quirks) are recorded in **`sprinter/README.md`** —
read its "Verified platform facts" section before touching the port's HAL.

The rest of this file documents the **original ZX Spectrum codebase** (the source
of truth being ported). Its build system, memory tricks, and ASM layer are
ZX-specific and largely do **not** carry over to the Sprinter target.

## What this is (original ZX Spectrum target)

SpecTalk ZX is an IRC client for the ZX Spectrum (8-bit Z80), built with the z88dk/SDCC cross-compiler. It targets real hardware: 48K/128K Spectrums with an ESP8266 WiFi module over a divMMC/divTIESUS hardware UART at 115200 baud, plus an SD card (esxDOS) for runtime data files. Everything is dominated by two scarce resources — **code size** and **RAM** — so most non-obvious code patterns exist to save bytes.

## Build

Requires `z88dk` (with SDCC), `python`, and GNU Make.

```bash
make            # CHECK -> CLEAN -> BPE -> BUILD -> TRIM -> OVERLAY -> INFO
make release    # max optimization (--max-allocs-per-node200000); use for releases
make nobpe      # build without BPE string compression (faster, larger; for debugging)
make clean      # remove build artifacts AND restore src/ from build/bpe_originals/
make help       # list targets
```

There is no test suite — verification is by building (warnings are errors via `-Cc--Werror` and `-Wall`) and running on emulator/hardware. Build output lands in `build/`: `SpecTalkZX.tap` (program), and the two SD-card data files `SPECTALK.DAT` and `SPECTALK.OVL`. A run needs all three files together on the SD card.

The build mutates `src/` in place (see BPE below). If a build is interrupted, run `make clean` to restore originals from `build/bpe_originals/` before doing anything else — otherwise you may be editing already-compressed source. There are hard size guards in the Makefile that fail the build (BSS overflow past `0xF500`, overlays > 2048B, `SPECTALK.DAT` ≠ 1517 bytes after restore); these are real invariants, not cosmetic.

## Architecture

### Unity Build (Single Compilation Unit)

`src/main_build.c` is the *only* C file passed to the compiler. It `#include`s the three real C modules **in a specific order**:

```c
#include "irc_handlers.c"   // IRC message parsing/dispatch
#include "user_cmds.c"      // /commands and !settings
#include "spectalk.c"       // main loop + ALL global variable definitions (must be last)
```

Implications you must respect when editing:
- `spectalk.c` defines the globals; the other two reference them via `extern` or forward declarations. It must stay last in the include order.
- All `static` functions are visible across modules (they're one translation unit). Name collisions between modules are silent — pick unique names.
- This is what enables SDCC's cross-function inlining and dead-code elimination. Don't split files into separately-compiled units.
- Some buffers are deliberately **aliased** to share RAM across disjoint lifetimes (e.g. `names_friend_buf` aliased onto `notif_buf`). These aliases are load-bearing memory savings; read the comments before touching them.

### ASM layer (`asm/`)

Hand-written Z80 for the hot paths and hardware. C calls into these via `__z88dk_callee` / `__z88dk_fastcall` / `__naked` (`ST_NAKED`) conventions — match the existing declarations exactly.
- `spectalk_asm.asm` — rendering (64-col custom font), input, ring-buffer push/pop, string/number utils, screen-address math, and `code_crt_init` BSS-zeroing (lets the TAP be truncated before BSS to save ~4KB).
- `divmmc_uart.asm` — UART driver (115200 baud, derived from Nihirash's driver).
- `overlay_loader.asm` — loads overlay blocks from `SPECTALK.OVL` into the ring buffer and executes them.

### Overlay system (`overlay/`)

Help, About, Config, Status, and What's New screens are **separately-compiled C overlays** loaded on demand from `SPECTALK.OVL` (4 × 2048-byte blocks) into the 2KB `ring_buffer` at `0xF500` — the same buffer used for UART reception, so the UART is drained before an overlay runs. Overlays cannot include `spectalk.h`; they use the self-contained ABI in `overlay/overlay_api.h`, whose addresses are resolved at link time from the resident binary's `.map` file via `tools/gen_overlay_defs.py`. Each overlay must stay ≤ 2048 bytes (Makefile enforces this).

### Memory map (fixed addresses)

RAM is hand-placed. Key fixed locations: `ring_buffer` = `0xF500` (also the overlay execution slot); BSS must end below `0xF500` (= 62720); Printer Buffer `0x5B00–0x5BFF` and CHANS workspace `0x5CB6–0x5DB5` are reused as scratch and zeroed at startup. Code origin (`ZORG`) is 24000, stack 512 bytes. Changing any of these without updating the ASM, the Makefile guards, and the linker expectations will break the build or crash on hardware.

### BPE string compression

`tools/bpe_build.py` (the `bpe` make phase) compresses ~155 screen strings using a token dictionary, rewriting `src/*.c` and `include/spectalk.h` in place and baking the dictionary into `SPECTALK.DAT`. Originals are backed up to `build/bpe_originals/` and restored after compile. **When editing source, always edit the originals in `src/`/`include/`, never the transient compressed versions.** Use `make nobpe` while iterating to avoid the rewrite churn.

## Editing conventions

- **Bytes matter more than clarity.** Existing code uses global parsing context instead of stack args, factored ASM subroutines, peephole rules, and buffer aliasing specifically to shrink the binary. Before "cleaning up" something that looks odd, check whether it's a deliberate size optimization (comments usually flag these with `OPT-*` / `STRUCT-*` tags).
- `src/spectalk_copt.rul` holds 35 custom z88dk peephole (copt) rules that factor repeated instruction sequences (e.g. `HL *= 32`) into subroutine calls. Adding/removing call sites can change which rules fire.
- Many comments are in Spanish (the author's language); keep that style when editing nearby code.
- Constants are duplicated across C and ASM by necessity (e.g. `RX_LINE_SIZE` in `spectalk.h` mirrors `RX_LINE_MAX` in `spectalk_asm.asm`). Comments mark these pairs — update both sides together.
- Bump `VERSION` in `include/spectalk.h`; the What's New overlay is generated from `CHANGELOG.md` by `tools/gen_whatsnew.py` at build time.

## Other tools (`tools/`)

Python font/data generators (`raster_ttf.py`, `ikkle_*` for the 4px mini font, `font4px_optimize.py`, `globe_pack.py`) regenerate baked-in binary assets. Most are run manually when assets change, not part of the default build; `bpe_build.py`, `gen_overlay_defs.py`, and `gen_whatsnew.py` are invoked by the Makefile.

## External reference sources (for the Sprinter port)

Local sibling repos to consult for Sprinter platform details, APIs, and idioms (see `plan.md` for which matters where):
- Platform docs/OS/BIOS: `/Users/dmitry/dev/zx/sprinter/sprinter_ai_doc/manual`, `…/sprinter_dss`, `…/sprinter_bios`
- Build SDKs: `…/sprinter/sdcc45-sprinter-sdk` (SDCC 4.5, preferred), `…/sprinter/sdcc-sprinter-sdk` (SDCC 2.9), `…/sprinter/zx-sprinter-sdk`, `/Users/dmitry/dev/zx/z88dk`
- Networking: `…/sprinter/sprinter_wifi` (ESP/SprinterWiFi), `…/sprinter/sprinter-rtl8019a` (ISA NE2000)
- Example Sprinter apps (build/idiom reference): `…/sprinter/sources/nupogodi`, `…/sprinter/gfxview`, `…/sprinter/gifview`, `…/sprinter/flexnavigator`, `…/sprinter/games/titd/src`, `…/sprinter/sources/sprinter-unzip`, `…/sprinter/sources/2DSTUDIO`, `…/sprinter/sources/DOOM2`, `…/sprinter/sources/{tasm_071/TASM,fformat/src/fformat_v113,fm/FM-SRC/FM}`
