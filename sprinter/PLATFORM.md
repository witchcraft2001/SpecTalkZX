# SprinTalk — Sprinter platform notes (developer)

Port of the ZX Spectrum IRC client **SpecTalk ZX** to the Sprinter computer
(DSS), built with the SDCC 4.5 Sprinter SDK. See `../plan.md` for the full plan
and `../CLAUDE.md` for context. Work proceeds in verifiable stages; this
directory is the Sprinter build, independent of the ZX `src/`/`asm/` tree.

(User-facing docs live in `README.md` and `HOWTO.md`; this file is the hard-won
platform/HAL reference.)

## Build

```sh
make            # -> SPTALK.EXE (DSS executable) + NETDIAG.EXE / NETIRC.EXE
make deploy     # build + write FAT12 floppy to distr/sptalk.img (EXEs, NET.CFG, docs)
make clean
```

Override the SDK path if needed: `make SDK=/path/to/sdcc45-sprinter-sdk`.
The build never writes into the SDK tree; the floppy image lives in `distr/`.

## Verify in MAME

```sh
./run.sh        # make deploy + launch MAME with distr/sptalk.img as flop2
```

At the DSS prompt, switch to the floppy drive and run `SPTALK`.

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
  region; text printed afterward keeps the region's ATTR (color). This is what
  makes per-nick colouring work: pre-set the nick cells' attribute, then `dss_puts`
  drops the characters on top without touching attributes.
- **Memory / flat WIN1+WIN2 layout:** a program loaded at 0x4200 owns WIN1
  (0x4000-0x7FFF) only. SPTALK now exceeds one page, so its image is built to span
  **WIN1+WIN2** as one load (code crosses 0x8000, so DSS maps both pages from the
  image itself) via the local `crt0_flat` — code low, data+stack high in WIN2
  (`--data-loc 0xA800`, stack `0xBFFF`). It must NOT `Dss.GetMem`+`SetWin2`: that
  swaps a blank page over the code already loaded into WIN2. `tools/check_layout.py`
  guards this at build time. The small diagnostics still use the SDK `crt0_page2`
  (single allocated WIN2 page, `--data-loc 0x8000`, stack `0xBFFF`).
- **Printing: use `dss_puts` (PCHARS #5C), not per-char `dss_putchar`** — one
  syscall per line vs one per character (~80× fewer syscalls; per-char redraw of
  the chat was the cause of multi-second startup).
- **ESP bring-up model (verified on real HW):** the SprinterWiFi kit's `NETUP`
  utility joins Wi-Fi as a one-time step and publishes the live network state in
  DSS environment variables (`NET`, `NET_BAUD`, `NET_IP`, …). Apps do NOT re-join —
  they read `NET`/`NET_BAUD`, init the local UART at that baud, and open TCP via
  `AT+CIPSTART` (transparent mode CIPMUX=0/CIPMODE=1), assuming the link is up.
  SPTALK refuses to run if `NET != WIFI` ("run NETUP first"). The TL16C550 UART
  probe returns PRESENT on the test machine.
- **Disconnect detection:** in transparent mode the ESP prints `\r\nCLOSED\r\n` on
  the UART when the socket closes; `net_poll` watches the RX stream for that token.
- **ISA slot is NOT fixed to ISA1.** `isa_open` maps WIN3 to slot byte
  `(slot<<1)|0xD4` → slot0=0xD4 (ISA1), slot1=0xD6 (ISA2). NETUP publishes the
  detected slot in env var `NET_ESP_HW` = `"<slot>/#3E8"` (first char = slot 0/1);
  `net_init` reads it and calls `isa_set_slot` before probing. `uart_probe` also
  scans both slots (and checks the IER hi-nibble reads 0, like the network lib's
  `UART_FIND`) as a fallback. Hardcoding 0xD4 = "ESP not found" when the card is in
  the other slot.
- **RX data loss: lower the FIFO trigger; do NOT toggle RTS by hand.** NETUP enables
  full hardware flow control on the ESP at setup — confirmed on HW: its
  `+UART_CUR:115273,8,1,0,3` line shows flow=3 even at the default baud (trailing `3`).
  So the ESP honours RTS/CTS and an app must NOT re-send `AT+UART_CUR` (needless
  baud-touch risk) nor turn flow off on exit. The loss came from the local 16550's
  AFE trigger being **8**: AFE deasserts RTS only once 8 bytes sit in the FIFO,
  leaving just 8 bytes of in-flight headroom — a slow-reacting ESP keeps sending past
  that during a long render (scroll + per-row draw) and overruns (lost MOTD chunks).
  Fix: lower the **RX trigger** so AFE drops RTS earlier. Now AFE drops RTS as soon as
  the FIFO reaches the trigger, so while net_poll isn't draining (during the render)
  RTS stays low and the ESP is held off the *whole* time — the same "pause during
  render" effect, but driven automatically by the FIFO level. **Do not** force RTS low
  manually via `MCR=0x20`: on real HW that wedged RX completely (RTS apparently stuck
  low / never resumed). Let AFE drive RTS off the FIFO — that is the mechanism that
  already worked at trigger 8, just safer. Tuning (verified on HW): trigger **1**
  (`FCR=0x07`) eliminated the loss but throttled bursts (MOTD slow); trigger **4**
  (`FCR=0x47`, 12 bytes headroom) is the chosen speed/safety compromise. Trigger 8
  (`FCR=0x81`) was the original lossy setting.
- **Scrolling: DSS `#55` (SCROLL) wedges** on a full-width region (it calls BIOS
  `#B7` internally; see `sprinter_dss/VIDEO.ASM`). The working approach (used by
  `texteditor`) is **BIOS `#8A` (LP_SCROLL_UD) directly**: `B`=dir (1=up, 2=down),
  `D`=top row (0-based), `E`=row count, wrap in `di`/`ei`. SPTALK uses this for the
  chat region.
- **Cyrillic = CP866** (lowercase 'е' = 0xA5); the high range 0x80–0xFF is
  printable and renders Cyrillic glyphs directly. Incoming UTF-8 is recoded to
  CP866 and outgoing CP866 to UTF-8 (toggle with `/encoding`).

## Stages

1. **HAL smoke test** (done) — 80x32 text, border, clock, keyboard, clean exit.
2. **Terminal HAL + chat layout** (done) — banner/chat/status/input, `dss_puts`
   redraw, input editor (cursor move, history), colours.
3. **net.cfg loader + ISA/UART diagnostics** (done) — `%NET_DIR%`/NET.CFG, ISA
   open/close discipline, TL16C550 probe. → `NETDIAG.EXE`. Verified on real HW.
4. **ESP TCP + IRC handshake** (done) — UART driver, ESP transparent mode, TCP to
   irc.libera.chat, NICK/USER, auto-PONG, session cleanup. → `NETIRC.EXE`.
5. **IRC engine + UI integration** (done) — windows, parser/dispatch, history.
6. **Feature parity** (in progress) — private msgs (`/query` `/msg`), `/me`,
   NickServ identify, nick colouring, env-var network config, recoding.
7. **NE2000 backend + packaging** (RTL build) — pending.
