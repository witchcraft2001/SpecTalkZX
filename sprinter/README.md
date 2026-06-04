# SpecTalk ZX — Sprinter DSS port

Port of the ZX Spectrum IRC client to the Sprinter computer (DSS), built with the
SDCC 4.5 Sprinter SDK. See `../plan.md` for the full plan and `../CLAUDE.md` for
context. Work proceeds in verifiable stages; this directory is the Sprinter
build, independent of the ZX `src/`/`asm/` tree.

## Build

```sh
make            # -> SPECTALK.EXE (DSS executable, load/entry 0x4100)
make deploy     # build EXE + write FAT12 floppy to distr/spectalk.img
make clean
```

Override the SDK path if needed: `make SDK=/path/to/sdcc45-sprinter-sdk`.
The build never writes into the SDK tree; the floppy image lives in `distr/`.

## Verify in MAME

```sh
./run.sh        # make deploy + launch MAME with distr/spectalk.img as flop2
```

At the DSS prompt, switch to the floppy drive and run `SPECTALK`.

## Verified platform facts (empirical, on MAME v3.06 / DSS)

- DSS launches programs already in **80x32 text mode** — no `dss_setvmod` needed.
- `dss_gotoxy(x, y)` is **1-based** (cols 1..80, rows 1..32).
- **Keyboard:** `dss_scankey` (#31) returns a *raw key code* in `.ascii`, not ASCII.
  For real ASCII use `dss_testkey` (#37) to peek (non-blocking, doesn't consume)
  then `dss_scankey` to pop. `dss_kbhit` maps to CTRLKEY/#33 (modifier state),
  **not** buffer state — don't use it for "key available".
- Key codes (`.ascii`): **ENTER = 0x0D, BACKSPACE = 0x08, ESC = 0x1B, TAB = 0x09**.
- Navigation keys carry no ASCII — identify them by `.scan`:
  **← 0x54, → 0x56, ↑ 0x58, ↓ 0x52, Home 0x57, End 0x51, PgUp 0x59, PgDn 0x53, Tab 0x0F**.
- **`dss_clear` / `dss_scroll` are 0-based**, but `dss_gotoxy` is **1-based**.
  Pass `x-1, y-1` to clear/scroll to align with gotoxy coordinates.
- `dss_clear(x, y, w, h, attr, fill)` sets both SYM (=fill char) and ATTR for the
  region; text printed afterward keeps the region's ATTR (color).
- **Memory: a program loaded at 0x4100 only owns WIN1 (0x4000-0x7FFF)** — the page
  DSS maps for the code. WIN2 (0x8000+) / WIN3 are *foreign pages* unless
  explicitly claimed via `Dss.GetMem` + `SetWin2/3`; the SDK default `--data-loc
  0x8000` / stack `0xBFFF` puts data+stack on a foreign page and gets silently
  corrupted by DSS/video. Fix: keep code+data+stack in WIN1 (`--data-loc 0x6000`,
  stack `0x7FFF`). crt0 does NOT zero `_DATA` (only `_BSS`, which is empty here),
  so explicitly init globals you read (or rely on write-before-read).
- **Printing: use `dss_puts` (PCHARS #5C), not per-char `dss_putchar`** — one
  syscall per line vs one per character (~80× fewer syscalls; per-char redraw of
  the chat was the cause of multi-second startup).
- **ESP bring-up model (verified on real HW):** the SprinterWiFi kit's `NETUP`
  utility joins Wi-Fi (AT+CWMODE/CWJAP from NET.CFG, DHCP/DNS, sets UART baud) as
  a one-time step. Apps (TCPTEST/PING/…) do NOT re-join — they init the local
  UART at the NET.CFG baud and open TCP via `AT+CIPSTART`, assuming the link is
  up. SpecTalk follows the same model: assume `NETUP` ran; do UART init +
  AT+CIPSTART + data, not AT+CWJAP. On the test machine `%NET_DIR%=\WIFI\`, and
  the TL16C550 UART probe returns PRESENT.
- **Scrolling: DSS `#55` (SCROLL) wedges** on a full-width region (it calls BIOS
  `#B7` internally; see `sprinter_dss/VIDEO.ASM`). The working approach (used by
  `texteditor`) is **BIOS `#8A` (LP_SCROLL_UP) directly**: `B`=dir (1=up, 0=down),
  `D`=start row, `E`=end row, wrap in `di`/`ei`. We currently use a RAM ring +
  `dss_puts` redraw instead; BIOS `#8A` is the O(1) optimization for later.
- **Cyrillic = CP866** (lowercase 'е' = 0xA5); the high range 0x80–0xFF is
  printable and renders Cyrillic glyphs directly.

## Stages

1. **HAL smoke test** (done) — 80x32 text, border, clock, keyboard, clean exit.
2. **Terminal HAL + chat layout** (done) — banner/chat/status/input, scrollback
   ring + `dss_puts`, input editor (cursor move, history), colors. → `SPECTALK.EXE`.
3. **net.cfg loader + ISA/UART diagnostics** (done) — reads `%NET_DIR%`/NET.CFG,
   ISA open/close discipline, TL16C550 probe. → `NETDIAG.EXE`. Verified on real HW.
4. **ESP TCP + IRC handshake** (done) — UART driver, ESP transparent mode
   (CIPMUX=0/CIPMODE=1), TCP to irc.libera.chat, NICK/USER, auto-PONG, session
   cleanup. → `NETIRC.EXE`. Verified on real HW: full Libera MOTD received.
5. Port IRC engine + UI integration.
5. IRC engine + UI integration.
6. Feature parity.
7. NE2000 backend + packaging.
